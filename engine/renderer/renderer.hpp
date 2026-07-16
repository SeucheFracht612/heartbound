#pragma once

#include "engine/core/result.hpp"
#include "engine/profiling/cpu_timing.hpp"
#include "engine/renderer/assets/sampler_cache.hpp"
#include "engine/renderer/assets/shader_manager.hpp"
#include "engine/renderer/assets/texture_manager.hpp"
#include "engine/renderer/chunks/chunk_render_system.hpp"
#include "engine/renderer/frame/frame_builder.hpp"
#include "engine/renderer/materials/material_runtime_cache.hpp"
#include "engine/renderer/materials/pipeline_cache.hpp"
#include "engine/renderer/render_camera.hpp"
#include "engine/renderer/renderer_stats.hpp"
#include "engine/renderer/rhi/render_device.hpp"
#include "engine/world/streaming/chunk_streamer.hpp"
#include "engine/world/world_state.hpp"

#include <chrono>
#include <array>
#include <memory>
#include <span>
#include <vector>

namespace heartstead::renderer {

struct RendererInitDesc {
    std::unique_ptr<rhi::IRenderDevice> device;
    std::vector<std::uint32_t> terrain_vertex_spirv;
    std::vector<std::uint32_t> terrain_fragment_spirv;
    const world::VoxelPalette* voxel_palette = nullptr;
    ChunkRenderConfig chunk_config{};
    ChunkGpuCacheConfig chunk_gpu_cache_config{};
    rhi::ClearColor clear_color{0.055F, 0.09F, 0.14F, 1.0F};
    rhi::RenderEnvironmentData environment{};
    bool development_shader_hot_reload = false;
};

class Renderer {
  public:
    Renderer() = default;
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    [[nodiscard]] core::Status initialize(RendererInitDesc desc);
    [[nodiscard]] core::Status shutdown();

    [[nodiscard]] core::Status synchronize_chunks(world::WorldState& world,
                                                  const RenderCamera& camera);
    [[nodiscard]] core::Status
    process_chunk_loads(std::span<const world::ChunkStreamLoadReport> loads);
    [[nodiscard]] core::Status
    process_chunk_evictions(std::span<const world::ChunkIdentity> evictions);
    [[nodiscard]] core::Status
    process_chunk_evictions(const world::ChunkStreamEvictionReport& eviction);

    [[nodiscard]] core::Result<rhi::RenderFrameStats> render(const RenderCamera& camera);
    [[nodiscard]] core::Status resize(rhi::RenderExtent extent);
    [[nodiscard]] core::Status set_environment(rhi::RenderEnvironmentData environment);
    [[nodiscard]] core::Status
    reload_terrain_shaders(std::span<const std::uint32_t> vertex_spirv,
                           std::span<const std::uint32_t> fragment_spirv);

    [[nodiscard]] bool is_initialized() const noexcept;
    [[nodiscard]] const ChunkRenderStats& chunk_stats() const noexcept;
    [[nodiscard]] const RendererStats& stats() const noexcept;
    [[nodiscard]] rhi::IRenderDevice* device() noexcept;
    [[nodiscard]] const rhi::IRenderDevice* device() const noexcept;

  private:
    [[nodiscard]] core::Status
    create_terrain_pipeline(std::span<const std::uint32_t> vertex_spirv,
                            std::span<const std::uint32_t> fragment_spirv,
                            const world::VoxelPalette* voxel_palette);
    void update_frontend_stats(std::size_t loaded_chunk_count) noexcept;
    void update_backend_stats(const rhi::RenderFrameStats& frame) noexcept;

    std::unique_ptr<rhi::IRenderDevice> device_;
    TerrainPipelineSet terrain_pipelines_{};
    std::array<GraphicsPipelineKey, 4> terrain_pipeline_keys_{};
    ShaderProgramHandle terrain_shader_program_;
    TextureHandle terrain_texture_array_;
    rhi::RenderResourceHandle terrain_sampler_;
    std::unique_ptr<ShaderManager> shader_manager_;
    std::unique_ptr<SamplerCache> sampler_cache_;
    std::unique_ptr<TextureManager> texture_manager_;
    std::unique_ptr<MaterialRuntimeCache> material_cache_;
    std::unique_ptr<PipelineCache> pipeline_cache_;
    std::unique_ptr<ChunkGpuCache> chunk_cache_;
    std::unique_ptr<ChunkRenderSystem> chunk_system_;
    std::unique_ptr<FrameBuilder> frame_builder_;
    profiling::CpuTimingRecorder cpu_timings_{};
    std::vector<rhi::RenderDrawCommand> chunk_draw_scratch_;
    RenderCommandLists draw_command_scratch_;
    rhi::RenderEnvironmentData environment_{};
    RendererStats stats_{};
    std::chrono::steady_clock::time_point frame_started_at_{};
    bool frame_timing_active_ = false;
};

} // namespace heartstead::renderer
