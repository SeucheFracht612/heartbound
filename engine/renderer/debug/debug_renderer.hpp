#pragma once

#include "engine/core/result.hpp"
#include "engine/math/matrix.hpp"
#include "engine/renderer/render_camera.hpp"
#include "engine/renderer/rhi/render_frame_plan.hpp"
#include "engine/world/coords/world_position.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace heartstead::renderer {

struct GpuDebugVertex {
    float position[3]{};
    float color[4]{1.0F, 1.0F, 1.0F, 1.0F};
};

static_assert(sizeof(GpuDebugVertex) == 28);
static_assert(offsetof(GpuDebugVertex, position) == 0);
static_assert(offsetof(GpuDebugVertex, color) == 12);

inline constexpr rhi::RenderVertexAttributeDesc gpu_debug_vertex_attributes[]{
    {0, offsetof(GpuDebugVertex, position), rhi::RenderVertexAttributeFormat::float3},
    {1, offsetof(GpuDebugVertex, color), rhi::RenderVertexAttributeFormat::float4},
};

enum class DebugDepthMode : std::uint8_t {
    depth_tested,
    overlay,
};

struct DebugLineDesc {
    world::WorldPosition start;
    world::WorldPosition end;
    std::array<float, 4> color{1.0F, 1.0F, 1.0F, 1.0F};
    float lifetime_seconds = 0.0F;
    DebugDepthMode depth_mode = DebugDepthMode::depth_tested;
};

struct DebugTextLabelDesc {
    world::WorldPosition position;
    std::string text;
    std::array<float, 4> color{1.0F, 1.0F, 1.0F, 1.0F};
    float lifetime_seconds = 0.0F;
    DebugDepthMode depth_mode = DebugDepthMode::overlay;
};

struct DebugTextLabelFrame {
    math::Vec3f camera_relative_position{};
    std::string text;
    std::array<float, 4> color{};
    DebugDepthMode depth_mode = DebugDepthMode::overlay;
};

struct DebugPipelineSet {
    rhi::RenderResourceHandle depth_tested;
    rhi::RenderResourceHandle overlay;

    [[nodiscard]] bool is_valid() const noexcept;
};

struct DebugRendererConfig {
    std::uint32_t maximum_lines = 65'536;
    std::uint32_t maximum_text_labels = 2'048;
    std::uint32_t buffered_frames = 3;

    [[nodiscard]] core::Status validate() const;
};

struct DebugRendererStats {
    std::uint32_t active_lines = 0;
    std::uint32_t active_text_labels = 0;
    std::uint32_t submitted_lines = 0;
    std::uint32_t draw_calls = 0;
    std::uint64_t overflowed_lines = 0;
    std::uint64_t overflowed_text_labels = 0;
    std::uint64_t uploaded_bytes = 0;
};

struct DebugFrameCommands {
    std::vector<rhi::RenderDrawCommand> draws;
    std::vector<DebugTextLabelFrame> text_labels;
    DebugRendererStats stats;
};

class DebugRenderer {
  public:
    DebugRenderer(rhi::IRenderDevice& device, DebugPipelineSet pipelines);
    ~DebugRenderer();

    DebugRenderer(const DebugRenderer&) = delete;
    DebugRenderer& operator=(const DebugRenderer&) = delete;

    [[nodiscard]] core::Status initialize(DebugRendererConfig config = {});
    [[nodiscard]] core::Status submit_line(DebugLineDesc line);
    [[nodiscard]] core::Status submit_ray(const world::WorldPosition& origin,
                                          math::Vec3f direction, float length,
                                          std::array<float, 4> color,
                                          float lifetime_seconds = 0.0F,
                                          DebugDepthMode depth = DebugDepthMode::depth_tested);
    [[nodiscard]] core::Status submit_aabb(const world::WorldPosition& origin,
                                           const math::Bounds3f& bounds,
                                           std::array<float, 4> color,
                                           float lifetime_seconds = 0.0F,
                                           DebugDepthMode depth = DebugDepthMode::depth_tested);
    [[nodiscard]] core::Status submit_oriented_box(
        const world::WorldPosition& origin, const math::Bounds3f& bounds,
        const math::Transform3f& transform, std::array<float, 4> color,
        float lifetime_seconds = 0.0F,
        DebugDepthMode depth = DebugDepthMode::depth_tested);
    [[nodiscard]] core::Status submit_sphere(const world::WorldPosition& origin, float radius,
                                             std::array<float, 4> color,
                                             std::uint32_t segments = 24,
                                             float lifetime_seconds = 0.0F,
                                             DebugDepthMode depth = DebugDepthMode::depth_tested);
    [[nodiscard]] core::Status submit_axes(const world::WorldPosition& origin, float scale = 1.0F,
                                           float lifetime_seconds = 0.0F,
                                           DebugDepthMode depth = DebugDepthMode::depth_tested);
    [[nodiscard]] core::Status submit_text(DebugTextLabelDesc label);

    [[nodiscard]] core::Result<DebugFrameCommands>
    build_frame(const RenderCamera& camera, float delta_seconds, DebugFrameCommands scratch = {});
    void clear() noexcept;
    [[nodiscard]] core::Status set_pipelines(DebugPipelineSet pipelines) noexcept;
    [[nodiscard]] core::Status shutdown();
    [[nodiscard]] const DebugRendererStats& stats() const noexcept;

  private:
    rhi::IRenderDevice* device_ = nullptr;
    DebugPipelineSet pipelines_{};
    DebugRendererConfig config_{};
    rhi::RenderResourceHandle vertex_buffer_;
    rhi::RenderResourceHandle index_buffer_;
    std::vector<DebugLineDesc> pending_lines_;
    std::vector<DebugLineDesc> active_lines_;
    std::vector<DebugTextLabelDesc> pending_labels_;
    std::vector<DebugTextLabelDesc> active_labels_;
    std::vector<GpuDebugVertex> vertex_scratch_;
    std::vector<std::uint32_t> index_scratch_;
    mutable std::mutex mutex_;
    std::uint64_t frame_number_ = 0;
    std::uint64_t overflowed_lines_ = 0;
    std::uint64_t overflowed_labels_ = 0;
    DebugRendererStats stats_{};
};

[[nodiscard]] core::Status validate_debug_line(const DebugLineDesc& line);
[[nodiscard]] core::Status validate_debug_text_label(const DebugTextLabelDesc& label);

} // namespace heartstead::renderer
