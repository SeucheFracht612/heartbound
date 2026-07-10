#include "engine/renderer/world_render_list.hpp"

#include <utility>

namespace heartstead::renderer {

core::Status anchor_mesh_draw(rhi::RenderMeshBinding& draw,
                              const world::WorldPosition& world_position,
                              world::FloatingOrigin floating_origin,
                              float maximum_camera_relative_extent) {
    auto relative =
        world::to_camera_relative(world_position, floating_origin, maximum_camera_relative_extent);
    if (!relative)
        return core::Status::failure(relative.error().code, relative.error().message);
    draw.world_anchor_x = world_position.anchor.x;
    draw.world_anchor_y = world_position.anchor.y;
    draw.world_anchor_z = world_position.anchor.z;
    draw.camera_relative_x = relative.value().x;
    draw.camera_relative_y = relative.value().y;
    draw.camera_relative_z = relative.value().z;
    return core::Status::ok();
}

core::Result<rhi::RenderMeshBinding> anchored_mesh_draw(rhi::RenderMeshBinding draw,
                                                        const world::WorldPosition& world_position,
                                                        world::FloatingOrigin floating_origin,
                                                        float maximum_camera_relative_extent) {
    auto status =
        anchor_mesh_draw(draw, world_position, floating_origin, maximum_camera_relative_extent);
    if (!status) {
        return core::Result<rhi::RenderMeshBinding>::failure(status.error().code,
                                                             status.error().message);
    }
    return core::Result<rhi::RenderMeshBinding>::success(std::move(draw));
}

} // namespace heartstead::renderer
