#include "engine/renderer/chunks/chunk_gpu_cache.hpp"

#include <limits>
#include <string>

namespace heartstead::renderer {

bool ChunkGpuEntry::has_drawable_mesh() const noexcept {
    return state == ChunkGpuState::resident && vertex_buffer.is_valid() &&
           index_buffer.is_valid() && vertex_count > 0 && index_count > 0;
}

std::size_t ChunkGpuEntry::resident_bytes() const noexcept {
    return vertex_bytes + index_bytes;
}

ChunkGpuCache::ChunkGpuCache(rhi::IRenderDevice& device) noexcept : device_(&device) {}

ChunkGpuCache::~ChunkGpuCache() {
    (void)clear();
}

core::Status ChunkGpuCache::insert(world::ChunkIdentity identity) {
    if (!identity.is_valid()) {
        return core::Status::failure("chunk_gpu_cache.invalid_identity",
                                     "chunk GPU entries require a valid load generation");
    }
    if (entries_.contains(identity)) {
        return core::Status::failure("chunk_gpu_cache.duplicate_identity",
                                     "chunk GPU identity is already cached");
    }
    if (find_by_coordinate(identity.coordinate) != nullptr) {
        return core::Status::failure(
            "chunk_gpu_cache.coordinate_generation_conflict",
            "a different load generation is still cached at the chunk coordinate");
    }

    ChunkGpuEntry entry;
    entry.identity = identity;
    entries_.emplace(identity, entry);
    refresh_current_stats();
    return core::Status::ok();
}

core::Status ChunkGpuCache::erase(world::ChunkIdentity identity) {
    const auto found = entries_.find(identity);
    if (found == entries_.end()) {
        const auto* coordinate_entry = find_by_coordinate(identity.coordinate);
        return core::Status::failure(
            coordinate_entry == nullptr ? "chunk_gpu_cache.missing_identity"
                                        : "chunk_gpu_cache.stale_load_generation",
            coordinate_entry == nullptr ? "chunk GPU identity is not cached"
                                        : "chunk GPU eviction references a stale load generation");
    }

    auto release_status = release_entry_resources(found->second);
    entries_.erase(found);
    refresh_current_stats();
    return release_status;
}

core::Status ChunkGpuCache::clear() {
    core::Status first_failure = core::Status::ok();
    for (const auto& [_, entry] : entries_) {
        auto status = release_entry_resources(entry);
        if (!status && first_failure) {
            first_failure = status;
        }
    }
    entries_.clear();
    refresh_current_stats();
    return first_failure;
}

bool ChunkGpuCache::contains(world::ChunkIdentity identity) const noexcept {
    return entries_.contains(identity);
}

const ChunkGpuEntry* ChunkGpuCache::find(world::ChunkIdentity identity) const noexcept {
    const auto found = entries_.find(identity);
    return found == entries_.end() ? nullptr : &found->second;
}

ChunkGpuEntry* ChunkGpuCache::find(world::ChunkIdentity identity) noexcept {
    const auto found = entries_.find(identity);
    return found == entries_.end() ? nullptr : &found->second;
}

const ChunkGpuEntry* ChunkGpuCache::find_by_coordinate(world::ChunkCoord coord) const noexcept {
    for (const auto& [identity, entry] : entries_) {
        if (identity.coordinate == coord) {
            return &entry;
        }
    }
    return nullptr;
}

std::vector<const ChunkGpuEntry*> ChunkGpuCache::entries() const {
    std::vector<const ChunkGpuEntry*> result;
    result.reserve(entries_.size());
    for (const auto& [_, entry] : entries_) {
        result.push_back(&entry);
    }
    return result;
}

void ChunkGpuCache::mark_cpu_mesh_ready(world::ChunkIdentity identity) noexcept {
    auto* entry = find(identity);
    if (entry != nullptr && entry->state != ChunkGpuState::resident) {
        entry->state = ChunkGpuState::cpu_mesh_ready;
    }
}

void ChunkGpuCache::mark_upload_pending(world::ChunkIdentity identity) noexcept {
    auto* entry = find(identity);
    if (entry != nullptr && entry->state != ChunkGpuState::resident) {
        entry->state = ChunkGpuState::upload_pending;
    }
}

void ChunkGpuCache::mark_failed(world::ChunkIdentity identity) noexcept {
    auto* entry = find(identity);
    if (entry != nullptr && entry->state != ChunkGpuState::resident) {
        entry->state = ChunkGpuState::failed;
    }
}

core::Result<ChunkGpuUploadResult> ChunkGpuCache::replace_mesh(
    world::ChunkIdentity identity, std::uint64_t content_revision, math::Bounds3f local_bounds,
    std::span<const terrain::GpuChunkVertex> vertices, std::span<const std::uint32_t> indices,
    std::uint64_t render_table_revision,
    std::span<const world::ChunkDependencyRevision> dependency_revisions) {
    auto* entry = find(identity);
    if (entry == nullptr) {
        return core::Result<ChunkGpuUploadResult>::failure(
            "chunk_gpu_cache.missing_identity", "cannot upload a mesh for an uncached chunk");
    }
    if (content_revision == 0) {
        return core::Result<ChunkGpuUploadResult>::failure(
            "chunk_gpu_cache.invalid_content_revision",
            "chunk GPU uploads require a nonzero content revision");
    }
    if (render_table_revision == 0) {
        return core::Result<ChunkGpuUploadResult>::failure(
            "chunk_gpu_cache.invalid_render_table_revision",
            "chunk GPU uploads require a nonzero block-render table revision");
    }
    if (vertices.empty() != indices.empty()) {
        return core::Result<ChunkGpuUploadResult>::failure(
            "chunk_gpu_cache.incomplete_mesh",
            "chunk GPU meshes must provide both vertex and index data or neither");
    }
    if (vertices.size() > std::numeric_limits<std::uint32_t>::max() ||
        indices.size() > std::numeric_limits<std::uint32_t>::max()) {
        return core::Result<ChunkGpuUploadResult>::failure(
            "chunk_gpu_cache.mesh_too_large", "chunk GPU mesh counts exceed uint32 draw limits");
    }

    ChunkGpuUploadResult result;
    result.empty = vertices.empty();
    result.replaced_resident_mesh = entry->state == ChunkGpuState::resident;
    const auto vertex_bytes = std::as_bytes(vertices);
    const auto index_bytes = std::as_bytes(indices);
    result.uploaded_bytes = vertex_bytes.size() + index_bytes.size();

    rhi::RenderResourceHandle new_vertex;
    rhi::RenderResourceHandle new_index;
    if (!vertices.empty()) {
        auto vertex_upload = device_->upload_buffer(
            {rhi::RenderBufferUsage::vertex, vertex_bytes.size(), "chunk_vertices"}, vertex_bytes);
        if (!vertex_upload) {
            mark_failed(identity);
            ++stats_.failed_upload_count;
            return core::Result<ChunkGpuUploadResult>::failure(vertex_upload.error().code,
                                                               vertex_upload.error().message);
        }
        new_vertex = vertex_upload.value().handle;

        auto index_upload = device_->upload_buffer(
            {rhi::RenderBufferUsage::index, index_bytes.size(), "chunk_indices"}, index_bytes);
        if (!index_upload) {
            (void)device_->release_resource(new_vertex);
            mark_failed(identity);
            ++stats_.failed_upload_count;
            return core::Result<ChunkGpuUploadResult>::failure(index_upload.error().code,
                                                               index_upload.error().message);
        }
        new_index = index_upload.value().handle;
    }

    const auto old_entry = *entry;
    entry->resident_content_revision = content_revision;
    entry->resident_render_table_revision = render_table_revision;
    entry->resident_dependency_revisions.assign(dependency_revisions.begin(),
                                                dependency_revisions.end());
    entry->vertex_buffer = new_vertex;
    entry->index_buffer = new_index;
    entry->vertex_count = static_cast<std::uint32_t>(vertices.size());
    entry->index_count = static_cast<std::uint32_t>(indices.size());
    entry->local_bounds = local_bounds;
    entry->state = ChunkGpuState::resident;
    entry->vertex_bytes = vertex_bytes.size();
    entry->index_bytes = index_bytes.size();

    auto release_status = release_entry_resources(old_entry);
    ++stats_.uploaded_chunk_count;
    stats_.uploaded_bytes += result.uploaded_bytes;
    refresh_current_stats();
    if (!release_status) {
        return core::Result<ChunkGpuUploadResult>::failure(release_status.error().code,
                                                           release_status.error().message);
    }
    return core::Result<ChunkGpuUploadResult>::success(result);
}

ChunkGpuCacheStats ChunkGpuCache::stats() const noexcept {
    return stats_;
}

core::Status ChunkGpuCache::release_entry_resources(const ChunkGpuEntry& entry) {
    core::Status first_failure = core::Status::ok();
    if (entry.vertex_buffer.is_valid()) {
        auto status = device_->release_resource(entry.vertex_buffer);
        if (!status) {
            first_failure = status;
        }
    }
    if (entry.index_buffer.is_valid()) {
        auto status = device_->release_resource(entry.index_buffer);
        if (!status && first_failure) {
            first_failure = status;
        }
    }
    return first_failure;
}

void ChunkGpuCache::refresh_current_stats() noexcept {
    stats_.entry_count = entries_.size();
    stats_.resident_chunk_count = 0;
    stats_.empty_chunk_count = 0;
    stats_.resident_buffer_count = 0;
    stats_.resident_bytes = 0;
    for (const auto& [_, entry] : entries_) {
        if (entry.state != ChunkGpuState::resident) {
            continue;
        }
        ++stats_.resident_chunk_count;
        stats_.resident_bytes += entry.resident_bytes();
        stats_.resident_buffer_count += static_cast<std::size_t>(entry.vertex_buffer.is_valid()) +
                                        static_cast<std::size_t>(entry.index_buffer.is_valid());
        if (!entry.has_drawable_mesh()) {
            ++stats_.empty_chunk_count;
        }
    }
}

} // namespace heartstead::renderer
