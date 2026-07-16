#pragma once

#include "engine/core/result.hpp"
#include "engine/renderer/frame/render_commands.hpp"
#include "engine/renderer/render_camera.hpp"
#include "engine/renderer/rhi/render_frame_plan.hpp"

namespace heartstead::renderer {

class FrameBuilder {
  public:
    explicit FrameBuilder(rhi::RenderExtent extent, rhi::ClearColor clear_color = {});

    [[nodiscard]] core::Status resize(rhi::RenderExtent extent);
    void set_clear_color(rhi::ClearColor clear_color) noexcept;

    [[nodiscard]] core::Result<rhi::RenderFramePlan> build_plan() const;
    [[nodiscard]] core::Result<rhi::RenderFrameSubmission> build(const RenderCamera& camera,
                                                                 RenderCommandLists commands) const;

    [[nodiscard]] rhi::RenderExtent extent() const noexcept;

  private:
    rhi::RenderExtent extent_{};
    rhi::ClearColor clear_color_{};
};

} // namespace heartstead::renderer
