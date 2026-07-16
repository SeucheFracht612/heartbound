#pragma once

#include "engine/renderer/rhi/render_frame_plan.hpp"

#include <vector>

namespace heartstead::renderer {

// Renderer-facing command lists. World/chunk concepts stop here and never enter IRenderDevice.
struct RenderCommandLists {
    std::vector<rhi::RenderDrawCommand> world_draws;
};

} // namespace heartstead::renderer
