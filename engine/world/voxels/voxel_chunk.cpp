#include "engine/world/voxels/voxel_chunk.hpp"

#include <algorithm>
#include <exception>
#include <limits>
#include <utility>

namespace heartstead::world {

namespace {

[[nodiscard]] constexpr std::uint8_t bit(ChunkDirtyFlag flag) noexcept {
    return static_cast<std::uint8_t>(flag);
}

} // namespace

void ChunkDirtyState::mark(ChunkDirtyFlag flag) noexcept {
    bits_ = static_cast<std::uint8_t>(bits_ | bit(flag));
}

void ChunkDirtyState::clear(ChunkDirtyFlag flag) noexcept {
    bits_ = static_cast<std::uint8_t>(bits_ & ~bit(flag));
}

void ChunkDirtyState::clear_all() noexcept {
    bits_ = 0;
}

bool ChunkDirtyState::contains(ChunkDirtyFlag flag) const noexcept {
    return (bits_ & bit(flag)) != 0;
}

std::uint8_t ChunkDirtyState::bits() const noexcept {
    return bits_;
}

VoxelChunk::VoxelChunk(ChunkCoord coord) : coord_(coord), cells_(total_cells, VoxelCell::air()) {}

ChunkCoord VoxelChunk::coord() const noexcept {
    return coord_;
}

ChunkIdentity VoxelChunk::identity() const noexcept {
    return {coord_, load_generation_};
}

std::uint64_t VoxelChunk::content_revision() const noexcept {
    return content_revision_;
}

const ChunkDirtyState& VoxelChunk::dirty() const noexcept {
    return dirty_;
}

core::Result<VoxelCell> VoxelChunk::get(VoxelCoord coord) const {
    if (!contains(coord)) {
        return core::Result<VoxelCell>::failure("chunk.coord_out_of_bounds",
                                                "voxel coordinate is outside the chunk");
    }

    return core::Result<VoxelCell>::success(cells_[index_of(coord)]);
}

core::Status VoxelChunk::set(VoxelCoord coord, VoxelCell cell) {
    if (!contains(coord)) {
        return core::Status::failure("chunk.coord_out_of_bounds",
                                     "voxel coordinate is outside the chunk");
    }

    auto& current = cells_[index_of(coord)];
    if (current == cell) {
        return core::Status::ok();
    }

    current = cell;
    advance_content_revision();
    dirty_.mark(ChunkDirtyFlag::mesh);
    dirty_.mark(ChunkDirtyFlag::collision);
    dirty_.mark(ChunkDirtyFlag::lighting);
    dirty_.mark(ChunkDirtyFlag::save);
    dirty_.mark(ChunkDirtyFlag::replication);
    return core::Status::ok();
}

core::Status VoxelChunk::apply_saved_cell(VoxelCoord coord, VoxelCell cell) {
    if (!contains(coord)) {
        return core::Status::failure("chunk.coord_out_of_bounds",
                                     "voxel coordinate is outside the chunk");
    }

    auto& current = cells_[index_of(coord)];
    if (current == cell) {
        return core::Status::ok();
    }

    current = cell;
    advance_content_revision();
    dirty_.mark(ChunkDirtyFlag::mesh);
    dirty_.mark(ChunkDirtyFlag::collision);
    dirty_.mark(ChunkDirtyFlag::lighting);
    return core::Status::ok();
}

core::Status VoxelChunk::load_generated_cells(std::vector<VoxelCell> cells) {
    if (cells.size() != total_cells) {
        return core::Status::failure("chunk.invalid_generated_cell_count",
                                     "generated chunk cell count does not match chunk size");
    }

    cells_ = std::move(cells);
    advance_content_revision();
    dirty_.clear_all();
    dirty_.mark(ChunkDirtyFlag::mesh);
    dirty_.mark(ChunkDirtyFlag::collision);
    dirty_.mark(ChunkDirtyFlag::lighting);
    return core::Status::ok();
}

void VoxelChunk::fill(VoxelCell cell) {
    if (std::ranges::all_of(cells_, [cell](const VoxelCell& current) { return current == cell; })) {
        return;
    }

    std::ranges::fill(cells_, cell);
    advance_content_revision();
    dirty_.mark(ChunkDirtyFlag::mesh);
    dirty_.mark(ChunkDirtyFlag::collision);
    dirty_.mark(ChunkDirtyFlag::lighting);
    dirty_.mark(ChunkDirtyFlag::save);
    dirty_.mark(ChunkDirtyFlag::replication);
}

void VoxelChunk::mark_dirty(ChunkDirtyFlag flag) noexcept {
    dirty_.mark(flag);
}

void VoxelChunk::clear_dirty(ChunkDirtyFlag flag) noexcept {
    dirty_.clear(flag);
}

void VoxelChunk::clear_all_dirty() noexcept {
    dirty_.clear_all();
}

void VoxelChunk::assign_load_generation(std::uint64_t generation) noexcept {
    load_generation_ = generation;
}

void VoxelChunk::advance_content_revision() noexcept {
    if (content_revision_ == std::numeric_limits<std::uint64_t>::max()) {
        std::terminate();
    }
    ++content_revision_;
}

bool VoxelChunk::contains(VoxelCoord coord) noexcept {
    return coord.x < edge_length && coord.y < edge_length && coord.z < edge_length;
}

std::size_t VoxelChunk::index_of(VoxelCoord coord) noexcept {
    constexpr auto edge = static_cast<std::size_t>(edge_length);
    return static_cast<std::size_t>(coord.z) * edge * edge +
           static_cast<std::size_t>(coord.y) * edge + static_cast<std::size_t>(coord.x);
}

} // namespace heartstead::world
