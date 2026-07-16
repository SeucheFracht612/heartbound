#pragma once

#include "engine/core/result.hpp"
#include "engine/profiling/cpu_timing.hpp"
#include "engine/renderer/camera/frustum.hpp"
#include "engine/renderer/chunks/chunk_gpu_cache.hpp"
#include "engine/renderer/render_camera.hpp"
#include "engine/renderer/rhi/render_frame_plan.hpp"
#include "engine/world/streaming/chunk_streamer.hpp"
#include "engine/world/world_state.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace heartstead::renderer {

struct ChunkRenderConfig {
    std::size_t max_chunks_meshed_per_frame = 4;
    std::size_t max_bytes_uploaded_per_frame = 8 * 1024 * 1024;

    [[nodiscard]] core::Status validate() const;
};

struct ChunkRenderStats {
    ChunkGpuCacheStats cache;
    std::size_t pending_mesh_count = 0;
    std::size_t pending_upload_count = 0;
    std::size_t pending_upload_bytes = 0;

    std::size_t meshed_chunk_count = 0;
    std::size_t uploaded_chunk_count = 0;
    std::size_t uploaded_bytes = 0;
    std::size_t failed_mesh_count = 0;
    std::size_t failed_upload_count = 0;

    std::size_t visible_chunk_count = 0;
    std::size_t culled_chunk_count = 0;
    std::size_t draw_count = 0;
    std::size_t visible_vertex_count = 0;
    std::size_t visible_index_count = 0;

    double culling_ms = 0.0;
    double draw_list_ms = 0.0;
    double chunk_snapshot_ms = 0.0;
    double meshing_ms = 0.0;
    double upload_preparation_ms = 0.0;
    double upload_ms = 0.0;
};

struct ChunkDrawList {
    std::vector<rhi::RenderDrawCommand> draws;
    std::size_t visible_chunk_count = 0;
    std::size_t culled_chunk_count = 0;
    std::size_t vertex_count = 0;
    std::size_t index_count = 0;
};

class ChunkRenderSystem {
  public:
    ChunkRenderSystem(ChunkGpuCache& cache, rhi::RenderResourceHandle terrain_pipeline,
                      const world::VoxelPalette* palette, ChunkRenderConfig config);

    [[nodiscard]] core::Status
    process_chunk_loads(std::span<const world::ChunkStreamLoadReport> loads);
    [[nodiscard]] core::Status
    process_chunk_evictions(std::span<const world::ChunkIdentity> evictions);
    [[nodiscard]] core::Status
    process_chunk_evictions(const world::ChunkStreamEvictionReport& eviction);

    [[nodiscard]] core::Status synchronize(world::WorldState& world, const RenderCamera& camera);
    [[nodiscard]] ChunkDrawList build_draw_list(const RenderCamera& camera);

    [[nodiscard]] const ChunkRenderStats& stats() const noexcept;

  private:
    struct PendingMesh {
        world::ChunkIdentity identity;
        bool forced = false;
        std::uint64_t sequence = 0;
    };

    struct PendingUpload {
        world::ChunkIdentity identity;
        std::uint64_t content_revision = 0;
        math::Bounds3f local_bounds{};
        std::vector<terrain::GpuChunkVertex> vertices;
        std::vector<std::uint32_t> indices;
        bool forced = false;
        std::uint64_t sequence = 0;

        [[nodiscard]] std::size_t byte_size() const noexcept;
    };

    void enqueue_mesh(world::ChunkIdentity identity, bool forced);
    void remove_pending(world::ChunkIdentity identity);
    [[nodiscard]] core::Status reconcile_loaded_chunks(world::WorldState& world);
    void consume_dirty_regions(world::WorldState& world);
    [[nodiscard]] core::Status process_mesh_queue(world::WorldState& world,
                                                  const RenderCamera& camera);
    [[nodiscard]] core::Status process_upload_queue(world::WorldState& world,
                                                    const RenderCamera& camera);
    void prioritize_mesh_queue(const world::WorldState& world, const RenderCamera& camera);
    void prioritize_upload_queue(const RenderCamera& camera);
    [[nodiscard]] bool is_visible(world::ChunkCoord coord, math::Bounds3f local_bounds,
                                  const RenderCamera& camera) const;
    [[nodiscard]] float distance_squared(world::ChunkCoord coord, math::Bounds3f local_bounds,
                                         const RenderCamera& camera) const;
    [[nodiscard]] static core::Result<math::Vec3f>
    camera_relative_chunk_origin(world::ChunkCoord coord, const RenderCamera& camera);
    void refresh_queue_stats() noexcept;
    void refresh_timing_stats() noexcept;

    ChunkGpuCache* cache_ = nullptr;
    rhi::RenderResourceHandle terrain_pipeline_;
    const world::VoxelPalette* palette_ = nullptr;
    ChunkRenderConfig config_{};
    std::vector<PendingMesh> pending_meshes_;
    std::vector<PendingUpload> pending_uploads_;
    std::uint64_t next_sequence_ = 1;
    ChunkRenderStats stats_{};
    profiling::CpuTimingRecorder timings_{};
};

} // namespace heartstead::renderer
