#pragma once

#include "engine/core/result.hpp"
#include "engine/renderer/chunks/chunk_render_system.hpp"
#include "engine/renderer/frame/frame_builder.hpp"
#include "engine/renderer/render_camera.hpp"
#include "engine/renderer/rhi/render_device.hpp"
#include "engine/world/streaming/chunk_streamer.hpp"
#include "engine/world/world_state.hpp"

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
    rhi::ClearColor clear_color{0.055F, 0.09F, 0.14F, 1.0F};
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

    [[nodiscard]] bool is_initialized() const noexcept;
    [[nodiscard]] const ChunkRenderStats& chunk_stats() const noexcept;
    [[nodiscard]] rhi::IRenderDevice* device() noexcept;
    [[nodiscard]] const rhi::IRenderDevice* device() const noexcept;

  private:
    [[nodiscard]] core::Status
    create_terrain_pipeline(std::span<const std::uint32_t> vertex_spirv,
                            std::span<const std::uint32_t> fragment_spirv);

    std::unique_ptr<rhi::IRenderDevice> device_;
    rhi::RenderResourceHandle terrain_vertex_shader_;
    rhi::RenderResourceHandle terrain_fragment_shader_;
    rhi::RenderResourceHandle terrain_pipeline_;
    std::unique_ptr<ChunkGpuCache> chunk_cache_;
    std::unique_ptr<ChunkRenderSystem> chunk_system_;
    std::unique_ptr<FrameBuilder> frame_builder_;
};

} // namespace heartstead::renderer
