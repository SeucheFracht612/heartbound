#pragma once

#include "engine/renderer/rhi/render_device.hpp"
#include "engine/world/coords/world_position.hpp"

namespace heartstead::renderer {

[[nodiscard]] core::Status anchor_mesh_draw(rhi::RenderMeshBinding& draw,
                                            const world::WorldPosition& world_position,
                                            world::FloatingOrigin floating_origin,
                                            float maximum_camera_relative_extent = 1'000'000.0F);

[[nodiscard]] core::Result<rhi::RenderMeshBinding>
anchored_mesh_draw(rhi::RenderMeshBinding draw, const world::WorldPosition& world_position,
                   world::FloatingOrigin floating_origin,
                   float maximum_camera_relative_extent = 1'000'000.0F);

} // namespace heartstead::renderer
