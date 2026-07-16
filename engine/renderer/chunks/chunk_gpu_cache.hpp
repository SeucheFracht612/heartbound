#pragma once

#include "engine/core/result.hpp"
#include "engine/math/vector.hpp"
#include "engine/renderer/rhi/render_device.hpp"
#include "engine/renderer/terrain/gpu_chunk_vertex.hpp"
#include "engine/world/chunks/chunk_identity.hpp"
#include "engine/world/meshing/chunk_mesh_snapshot.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <vector>

namespace heartstead::renderer {

enum class ChunkGpuState {
    missing,
    cpu_mesh_ready,
    upload_pending,
    resident,
    failed,
};

struct ChunkGpuEntry {
    world::ChunkIdentity identity;
    std::uint64_t resident_content_revision = 0;
    std::uint64_t resident_render_table_revision = 0;
    std::vector<world::ChunkDependencyRevision> resident_dependency_revisions;

    rhi::RenderResourceHandle vertex_buffer;
    rhi::RenderResourceHandle index_buffer;
    std::uint32_t vertex_count = 0;
    std::uint32_t index_count = 0;

    math::Bounds3f local_bounds{};
    ChunkGpuState state = ChunkGpuState::missing;
    std::size_t vertex_bytes = 0;
    std::size_t index_bytes = 0;

    [[nodiscard]] bool has_drawable_mesh() const noexcept;
    [[nodiscard]] std::size_t resident_bytes() const noexcept;
};

struct ChunkGpuCacheStats {
    std::size_t entry_count = 0;
    std::size_t resident_chunk_count = 0;
    std::size_t empty_chunk_count = 0;
    std::size_t resident_buffer_count = 0;
    std::size_t resident_bytes = 0;
    std::uint64_t uploaded_chunk_count = 0;
    std::uint64_t uploaded_bytes = 0;
    std::uint64_t failed_upload_count = 0;
};

struct ChunkGpuUploadResult {
    std::size_t uploaded_bytes = 0;
    bool empty = false;
    bool replaced_resident_mesh = false;
};

class ChunkGpuCache {
  public:
    explicit ChunkGpuCache(rhi::IRenderDevice& device) noexcept;
    ~ChunkGpuCache();

    ChunkGpuCache(const ChunkGpuCache&) = delete;
    ChunkGpuCache& operator=(const ChunkGpuCache&) = delete;

    [[nodiscard]] core::Status insert(world::ChunkIdentity identity);
    [[nodiscard]] core::Status erase(world::ChunkIdentity identity);
    [[nodiscard]] core::Status clear();

    [[nodiscard]] bool contains(world::ChunkIdentity identity) const noexcept;
    [[nodiscard]] const ChunkGpuEntry* find(world::ChunkIdentity identity) const noexcept;
    [[nodiscard]] ChunkGpuEntry* find(world::ChunkIdentity identity) noexcept;
    [[nodiscard]] const ChunkGpuEntry* find_by_coordinate(world::ChunkCoord coord) const noexcept;
    [[nodiscard]] std::vector<const ChunkGpuEntry*> entries() const;

    void mark_cpu_mesh_ready(world::ChunkIdentity identity) noexcept;
    void mark_upload_pending(world::ChunkIdentity identity) noexcept;
    void mark_failed(world::ChunkIdentity identity) noexcept;

    [[nodiscard]] core::Result<ChunkGpuUploadResult>
    replace_mesh(world::ChunkIdentity identity, std::uint64_t content_revision,
                 math::Bounds3f local_bounds, std::span<const terrain::GpuChunkVertex> vertices,
                 std::span<const std::uint32_t> indices, std::uint64_t render_table_revision = 1,
                 std::span<const world::ChunkDependencyRevision> dependency_revisions = {});

    [[nodiscard]] ChunkGpuCacheStats stats() const noexcept;

  private:
    [[nodiscard]] core::Status release_entry_resources(const ChunkGpuEntry& entry);
    void refresh_current_stats() noexcept;

    rhi::IRenderDevice* device_ = nullptr;
    std::map<world::ChunkIdentity, ChunkGpuEntry> entries_;
    ChunkGpuCacheStats stats_{};
};

} // namespace heartstead::renderer
