#include "engine/world/chunks/chunk_database.hpp"

#include "engine/world/voxels/voxel_palette.hpp"

#include <algorithm>
#include <atomic>
#include <exception>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <utility>

namespace heartstead::world {

namespace {

std::atomic<std::uint64_t> next_chunk_load_generation{1};

[[nodiscard]] std::uint64_t allocate_load_generation() noexcept {
    const auto generation = next_chunk_load_generation.fetch_add(1, std::memory_order_relaxed);
    // Exhausting every 64-bit generation is not recoverable without reusing an identity. Preserve
    // zero as the invalid generation even in that physically unreachable situation.
    if (generation == 0) {
        std::terminate();
    }
    return generation;
}

[[nodiscard]] std::uint64_t mix(std::uint64_t value) noexcept {
    value ^= value >> 30U;
    value *= 0xbf58476d1ce4e5b9ULL;
    value ^= value >> 27U;
    value *= 0x94d049bb133111ebULL;
    value ^= value >> 31U;
    return value;
}

[[nodiscard]] std::uint64_t coordinate_bits(std::int64_t value) noexcept {
    return static_cast<std::uint64_t>(value) ^ 0x8000000000000000ULL;
}

} // namespace

std::size_t ChunkDatabase::ChunkCoordHash::operator()(ChunkCoord coord) const noexcept {
    const auto x = mix(coordinate_bits(coord.x));
    const auto y = mix(coordinate_bits(coord.y) ^ 0x9e3779b97f4a7c15ULL);
    const auto z = mix(coordinate_bits(coord.z) ^ 0xbf58476d1ce4e5b9ULL);
    return static_cast<std::size_t>(x ^ y ^ z);
}

VoxelChunk& ChunkDatabase::get_or_create(ChunkCoord coord) {
    const auto [it, inserted] = chunks_.try_emplace(coord, coord);
    if (inserted) {
        it->second.assign_load_generation(allocate_load_generation());
    }
    return it->second;
}

VoxelChunk* ChunkDatabase::find(ChunkCoord coord) noexcept {
    const auto found = chunks_.find(coord);
    return found == chunks_.end() ? nullptr : &found->second;
}

const VoxelChunk* ChunkDatabase::find(ChunkCoord coord) const noexcept {
    const auto found = chunks_.find(coord);
    return found == chunks_.end() ? nullptr : &found->second;
}

std::vector<const VoxelChunk*> ChunkDatabase::records() const {
    std::vector<const VoxelChunk*> result;
    result.reserve(chunks_.size());
    for (const auto& [_, chunk] : chunks_) {
        result.push_back(&chunk);
    }
    return result;
}

std::vector<ChunkIdentity> ChunkDatabase::identities() const {
    std::vector<ChunkIdentity> result;
    result.reserve(chunks_.size());
    for (const auto& [_, chunk] : chunks_) {
        result.push_back(chunk.identity());
    }
    std::ranges::sort(result);
    return result;
}

bool ChunkDatabase::contains(ChunkCoord coord) const noexcept {
    return find(coord) != nullptr;
}

std::size_t ChunkDatabase::chunk_count() const noexcept {
    return chunks_.size();
}

bool ChunkDatabase::erase(ChunkCoord coord) {
    if (chunks_.erase(coord) == 0) {
        return false;
    }
    for (const auto neighbor : face_neighbors(coord)) {
        auto* chunk = find(neighbor);
        if (chunk != nullptr) {
            chunk->mark_dirty(ChunkDirtyFlag::mesh);
            chunk->mark_dirty(ChunkDirtyFlag::collision);
            chunk->mark_dirty(ChunkDirtyFlag::lighting);
        }
    }
    return true;
}

core::Status ChunkDatabase::insert_generated(VoxelChunk chunk) {
    return insert_generated_impl(std::move(chunk), nullptr);
}

core::Status ChunkDatabase::insert_generated(VoxelChunk chunk,
                                             dirty::DirtyRegionTracker& dirty_regions) {
    auto staged_dirty = dirty_regions;
    auto status = insert_generated_impl(std::move(chunk), &staged_dirty);
    if (status) {
        dirty_regions = std::move(staged_dirty);
    }
    return status;
}

core::Status
ChunkDatabase::insert_generated_with_saved_edits(VoxelChunk chunk,
                                                 std::span<const VoxelEditRecord> edits) {
    return insert_generated_with_saved_edits_impl(std::move(chunk), edits, nullptr);
}

core::Status
ChunkDatabase::insert_generated_with_saved_edits(VoxelChunk chunk,
                                                 std::span<const VoxelEditRecord> edits,
                                                 dirty::DirtyRegionTracker& dirty_regions) {
    auto staged_dirty = dirty_regions;
    auto status = insert_generated_with_saved_edits_impl(std::move(chunk), edits, &staged_dirty);
    if (status) {
        dirty_regions = std::move(staged_dirty);
    }
    return status;
}

core::Result<VoxelCell> ChunkDatabase::get(ChunkCoord chunk_coord, VoxelCoord voxel_coord) const {
    const auto* chunk = find(chunk_coord);
    if (chunk == nullptr) {
        return core::Result<VoxelCell>::failure("chunk_database.missing_chunk",
                                                "chunk does not exist");
    }
    return chunk->get(voxel_coord);
}

core::Status ChunkDatabase::set(ChunkCoord chunk_coord, VoxelCoord voxel_coord, VoxelCell cell) {
    auto& chunk = get_or_create(chunk_coord);
    auto previous = chunk.get(voxel_coord);
    if (!previous) {
        return core::Status::failure(previous.error().code, previous.error().message);
    }
    if (previous.value() == cell) {
        return core::Status::ok();
    }

    auto status = chunk.set(voxel_coord, cell);
    if (!status) {
        return status;
    }

    edit_log_.push_back(VoxelEditRecord{chunk_coord, voxel_coord, previous.value(), cell});
    mark_neighbor_dirty_if_boundary(chunk_coord, voxel_coord, nullptr);
    return core::Status::ok();
}

core::Status ChunkDatabase::set(ChunkCoord chunk_coord, VoxelCoord voxel_coord, VoxelCell cell,
                                dirty::DirtyRegionTracker& dirty_regions) {
    auto previous = find(chunk_coord) != nullptr ? find(chunk_coord)->get(voxel_coord)
                                                 : VoxelChunk(chunk_coord).get(voxel_coord);
    if (!previous)
        return core::Status::failure(previous.error().code, previous.error().message);
    if (previous.value() == cell)
        return core::Status::ok();
    auto staged_dirty = dirty_regions;
    auto status = set(chunk_coord, voxel_coord, cell);
    if (!status)
        return status;
    status = mark_chunk_rebuild_regions(staged_dirty, chunk_coord, "voxel edit");
    if (!status)
        return status;
    mark_neighbor_dirty_if_boundary(chunk_coord, voxel_coord, &staged_dirty);
    dirty_regions = std::move(staged_dirty);
    return core::Status::ok();
}

core::Status ChunkDatabase::set(ChunkCoord chunk_coord, VoxelCoord voxel_coord, VoxelCell cell,
                                dirty::DirtyRegionTracker& dirty_regions,
                                const VoxelPalette& palette) {
    auto previous = find(chunk_coord) != nullptr ? find(chunk_coord)->get(voxel_coord)
                                                 : VoxelChunk(chunk_coord).get(voxel_coord);
    if (!previous) {
        return core::Status::failure(previous.error().code, previous.error().message);
    }
    if (previous.value() == cell) {
        return core::Status::ok();
    }

    auto staged_dirty = dirty_regions;
    auto status = set(chunk_coord, voxel_coord, cell, staged_dirty);
    if (!status)
        return status;
    const auto radius = std::max(palette.mesh_invalidation_radius(previous.value()),
                                 palette.mesh_invalidation_radius(cell));
    status = mark_rich_mesh_invalidation(chunk_coord, voxel_coord, radius, staged_dirty);
    if (status) {
        dirty_regions = std::move(staged_dirty);
    }
    return status;
}

core::Status ChunkDatabase::apply_saved_edits(std::span<const VoxelEditRecord> edits) {
    return apply_saved_edits_impl(edits, nullptr);
}

core::Status ChunkDatabase::apply_saved_edits(std::span<const VoxelEditRecord> edits,
                                              dirty::DirtyRegionTracker& dirty_regions) {
    auto staged_dirty = dirty_regions;
    auto status = apply_saved_edits_impl(edits, &staged_dirty);
    if (status) {
        dirty_regions = std::move(staged_dirty);
    }
    return status;
}

const std::vector<VoxelEditRecord>& ChunkDatabase::edit_log() const noexcept {
    return edit_log_;
}

void ChunkDatabase::clear_edit_log() {
    edit_log_.clear();
}

void ChunkDatabase::clear_all_dirty() {
    for (auto& [_, chunk] : chunks_) {
        chunk.clear_all_dirty();
    }
}

ChunkDatabaseStats ChunkDatabase::stats() const noexcept {
    ChunkDatabaseStats result;
    result.chunk_count = chunks_.size();
    result.edit_count = edit_log_.size();
    for (const auto& [_, chunk] : chunks_) {
        if (chunk.dirty().contains(ChunkDirtyFlag::mesh)) {
            ++result.dirty_mesh_count;
        }
        if (chunk.dirty().contains(ChunkDirtyFlag::save)) {
            ++result.dirty_save_count;
        }
        if (chunk.dirty().contains(ChunkDirtyFlag::replication)) {
            ++result.dirty_replication_count;
        }
    }
    return result;
}

core::Status ChunkDatabase::insert_generated_impl(VoxelChunk chunk,
                                                  dirty::DirtyRegionTracker* dirty_regions) {
    const auto coord = chunk.coord();
    if (contains(coord)) {
        return core::Status::failure("chunk_database.duplicate_generated_chunk",
                                     "generated chunk already exists");
    }

    if (dirty_regions != nullptr) {
        auto status = mark_chunk_rebuild_regions(*dirty_regions, coord, "generated chunk");
        if (!status) {
            return status;
        }
        for (const auto neighbor : face_neighbors(coord)) {
            if (find(neighbor) == nullptr) {
                continue;
            }
            status = mark_chunk_rebuild_regions(*dirty_regions, neighbor,
                                                "neighbor chunk became resident");
            if (!status) {
                return status;
            }
        }
    }

    chunk.mark_dirty(ChunkDirtyFlag::mesh);
    chunk.mark_dirty(ChunkDirtyFlag::collision);
    chunk.mark_dirty(ChunkDirtyFlag::lighting);
    chunk.assign_load_generation(allocate_load_generation());
    chunks_.emplace(coord, std::move(chunk));
    for (const auto neighbor : face_neighbors(coord)) {
        auto* resident = find(neighbor);
        if (resident != nullptr) {
            resident->mark_dirty(ChunkDirtyFlag::mesh);
            resident->mark_dirty(ChunkDirtyFlag::collision);
            resident->mark_dirty(ChunkDirtyFlag::lighting);
        }
    }
    return core::Status::ok();
}

core::Status
ChunkDatabase::insert_generated_with_saved_edits_impl(VoxelChunk chunk,
                                                      std::span<const VoxelEditRecord> edits,
                                                      dirty::DirtyRegionTracker* dirty_regions) {
    const auto coord = chunk.coord();
    if (edits.empty()) {
        return core::Status::failure("chunk_database.empty_saved_edit_batch",
                                     "generated chunk saved edit batch must not be empty");
    }

    auto status = validate_saved_edit_batch(edits, &coord);
    if (!status) {
        return status;
    }

    // Apply to the not-yet-visible chunk first. A malformed batch can therefore never leave a
    // generated or partially restored chunk in the live database.
    for (const auto& edit : edits) {
        status = chunk.apply_saved_cell(edit.voxel_coord, edit.next);
        if (!status) {
            return status;
        }
    }

    status = insert_generated_impl(std::move(chunk), dirty_regions);
    if (!status) {
        return status;
    }

    replace_saved_edit_history(edits);
    for (const auto& edit : edits) {
        mark_neighbor_dirty_if_boundary(edit.chunk_coord, edit.voxel_coord, dirty_regions);
    }
    return core::Status::ok();
}

core::Status ChunkDatabase::apply_saved_edits_impl(std::span<const VoxelEditRecord> edits,
                                                   dirty::DirtyRegionTracker* dirty_regions) {
    auto status = validate_saved_edit_batch(edits);
    if (!status) {
        return status;
    }

    for (const auto& edit : edits) {
        auto& chunk = get_or_create(edit.chunk_coord);
        status = chunk.apply_saved_cell(edit.voxel_coord, edit.next);
        if (!status) {
            return status;
        }

        if (dirty_regions != nullptr) {
            status = mark_chunk_rebuild_regions(*dirty_regions, edit.chunk_coord,
                                                "saved voxel edit delta");
            if (!status) {
                return status;
            }
        }
        mark_neighbor_dirty_if_boundary(edit.chunk_coord, edit.voxel_coord, dirty_regions);
    }

    // A persisted batch is the canonical full delta for every chunk it names. Replace any
    // retained history from an earlier load instead of appending it again on every stream cycle.
    replace_saved_edit_history(edits);
    return core::Status::ok();
}

core::Status ChunkDatabase::validate_saved_edit_batch(std::span<const VoxelEditRecord> edits,
                                                      const ChunkCoord* expected_chunk) {
    for (const auto& edit : edits) {
        if (expected_chunk != nullptr && edit.chunk_coord != *expected_chunk) {
            return core::Status::failure("chunk_database.saved_edit_chunk_mismatch",
                                         "saved edit batch contains an edit for a different chunk");
        }
        if (edit.voxel_coord.x >= VoxelChunk::edge_length ||
            edit.voxel_coord.y >= VoxelChunk::edge_length ||
            edit.voxel_coord.z >= VoxelChunk::edge_length) {
            return core::Status::failure(
                "chunk_database.saved_edit_coord_out_of_bounds",
                "saved edit batch contains a local voxel coordinate outside the chunk");
        }
    }
    return core::Status::ok();
}

void ChunkDatabase::replace_saved_edit_history(std::span<const VoxelEditRecord> edits) {
    std::set<ChunkCoord> touched_chunks;
    for (const auto& edit : edits) {
        touched_chunks.insert(edit.chunk_coord);
    }
    if (touched_chunks.empty()) {
        return;
    }

    std::erase_if(edit_log_, [&touched_chunks](const VoxelEditRecord& existing) {
        return touched_chunks.contains(existing.chunk_coord);
    });
    edit_log_.insert(edit_log_.end(), edits.begin(), edits.end());
}

void ChunkDatabase::mark_neighbor_dirty_if_boundary(ChunkCoord chunk_coord, VoxelCoord voxel_coord,
                                                    dirty::DirtyRegionTracker* dirty_regions) {
    for (const auto neighbor : boundary_neighbors(chunk_coord, voxel_coord)) {
        auto* chunk = find(neighbor);
        if (chunk != nullptr) {
            chunk->mark_dirty(ChunkDirtyFlag::mesh);
            chunk->mark_dirty(ChunkDirtyFlag::collision);
            chunk->mark_dirty(ChunkDirtyFlag::lighting);
            if (dirty_regions != nullptr) {
                (void)mark_chunk_rebuild_regions(*dirty_regions, neighbor, "boundary voxel edit");
            }
        }
    }
}

core::Status ChunkDatabase::mark_rich_mesh_invalidation(ChunkCoord chunk_coord,
                                                        VoxelCoord voxel_coord,
                                                        std::uint16_t radius,
                                                        dirty::DirtyRegionTracker& dirty_regions) {
    if (radius == 0) {
        return core::Status::ok();
    }

    const auto floor_chunk_offset = [](std::int32_t cell) noexcept {
        constexpr auto edge = static_cast<std::int32_t>(VoxelChunk::edge_length);
        if (cell >= 0) {
            return cell / edge;
        }
        return -((-cell + edge - 1) / edge);
    };
    const auto checked_offset = [](std::int64_t value,
                                   std::int32_t offset) -> std::optional<std::int64_t> {
        if (offset > 0 && value > std::numeric_limits<std::int64_t>::max() - offset) {
            return std::nullopt;
        }
        if (offset < 0 && value < std::numeric_limits<std::int64_t>::min() - offset) {
            return std::nullopt;
        }
        return value + offset;
    };

    const auto signed_radius = static_cast<std::int32_t>(radius);
    const auto min_x = floor_chunk_offset(static_cast<std::int32_t>(voxel_coord.x) - signed_radius);
    const auto max_x = floor_chunk_offset(static_cast<std::int32_t>(voxel_coord.x) + signed_radius);
    const auto min_y = floor_chunk_offset(static_cast<std::int32_t>(voxel_coord.y) - signed_radius);
    const auto max_y = floor_chunk_offset(static_cast<std::int32_t>(voxel_coord.y) + signed_radius);
    const auto min_z = floor_chunk_offset(static_cast<std::int32_t>(voxel_coord.z) - signed_radius);
    const auto max_z = floor_chunk_offset(static_cast<std::int32_t>(voxel_coord.z) + signed_radius);

    for (auto z = min_z; z <= max_z; ++z) {
        for (auto y = min_y; y <= max_y; ++y) {
            for (auto x = min_x; x <= max_x; ++x) {
                if (x == 0 && y == 0 && z == 0) {
                    continue;
                }
                const auto neighbor_x = checked_offset(chunk_coord.x, x);
                const auto neighbor_y = checked_offset(chunk_coord.y, y);
                const auto neighbor_z = checked_offset(chunk_coord.z, z);
                if (!neighbor_x || !neighbor_y || !neighbor_z) {
                    continue;
                }
                const ChunkCoord neighbor{*neighbor_x, *neighbor_y, *neighbor_z};
                auto* neighbor_chunk = find(neighbor);
                if (neighbor_chunk == nullptr) {
                    continue;
                }
                neighbor_chunk->mark_dirty(ChunkDirtyFlag::mesh);
                auto status = dirty_regions.mark_single(dirty::DirtyRegionKind::chunk_mesh,
                                                        dirty_coord_for_chunk(neighbor),
                                                        "rich block mesh invalidation");
                if (!status) {
                    return status;
                }
            }
        }
    }
    return core::Status::ok();
}

dirty::DirtyRegionCoord ChunkDatabase::dirty_coord_for_chunk(ChunkCoord coord) noexcept {
    return {coord.x, coord.y, coord.z};
}

core::Status ChunkDatabase::mark_chunk_rebuild_regions(dirty::DirtyRegionTracker& dirty_regions,
                                                       ChunkCoord coord, std::string reason) {
    const auto dirty_coord = dirty_coord_for_chunk(coord);
    auto status =
        dirty_regions.mark_single(dirty::DirtyRegionKind::chunk_mesh, dirty_coord, reason);
    if (!status) {
        return status;
    }
    status =
        dirty_regions.mark_single(dirty::DirtyRegionKind::chunk_collision, dirty_coord, reason);
    if (!status) {
        return status;
    }
    return dirty_regions.mark_single(dirty::DirtyRegionKind::chunk_lighting, dirty_coord,
                                     std::move(reason));
}

std::vector<ChunkCoord> ChunkDatabase::boundary_neighbors(ChunkCoord chunk_coord,
                                                          VoxelCoord voxel_coord) {
    std::vector<ChunkCoord> result;
    if (voxel_coord.x == 0) {
        if (chunk_coord.x > std::numeric_limits<std::int64_t>::min()) {
            result.push_back({chunk_coord.x - 1, chunk_coord.y, chunk_coord.z});
        }
    }
    if (voxel_coord.x + 1 == VoxelChunk::edge_length) {
        if (chunk_coord.x < std::numeric_limits<std::int64_t>::max()) {
            result.push_back({chunk_coord.x + 1, chunk_coord.y, chunk_coord.z});
        }
    }
    if (voxel_coord.y == 0) {
        if (chunk_coord.y > std::numeric_limits<std::int64_t>::min()) {
            result.push_back({chunk_coord.x, chunk_coord.y - 1, chunk_coord.z});
        }
    }
    if (voxel_coord.y + 1 == VoxelChunk::edge_length) {
        if (chunk_coord.y < std::numeric_limits<std::int64_t>::max()) {
            result.push_back({chunk_coord.x, chunk_coord.y + 1, chunk_coord.z});
        }
    }
    if (voxel_coord.z == 0) {
        if (chunk_coord.z > std::numeric_limits<std::int64_t>::min()) {
            result.push_back({chunk_coord.x, chunk_coord.y, chunk_coord.z - 1});
        }
    }
    if (voxel_coord.z + 1 == VoxelChunk::edge_length) {
        if (chunk_coord.z < std::numeric_limits<std::int64_t>::max()) {
            result.push_back({chunk_coord.x, chunk_coord.y, chunk_coord.z + 1});
        }
    }
    return result;
}

std::vector<ChunkCoord> ChunkDatabase::face_neighbors(ChunkCoord chunk_coord) {
    auto result = boundary_neighbors(chunk_coord, {0, 0, 0});
    auto positive =
        boundary_neighbors(chunk_coord, {VoxelChunk::edge_length - 1, VoxelChunk::edge_length - 1,
                                         VoxelChunk::edge_length - 1});
    result.insert(result.end(), positive.begin(), positive.end());
    return result;
}

} // namespace heartstead::world
