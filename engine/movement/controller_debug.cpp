#include "engine/movement/controller_debug.hpp"

#include <cmath>
#include <string>

namespace heartstead::movement {

core::Status submit_player_controller_debug(debug::DebugOverlayRegistry& overlays,
                                             const PlayerController& controller,
                                             const PlayerControllerState& state,
                                             double prediction_error) {
    if (!std::isfinite(prediction_error) || prediction_error < 0.0) {
        return core::Status::failure("controller_debug.invalid_prediction_error",
                                     "controller prediction error must be finite and non-negative");
    }
    const auto shape = controller.shape_for(state);
    auto center = world::WorldPosition::from_anchor(
        state.position.anchor,
        state.position.local_offset + math::Vec3d{0.0, shape.height * 0.5, 0.0});
    if (!center) {
        return core::Status::failure(center.error().code, center.error().message);
    }
    debug::DebugOverlayPrimitive collider;
    collider.overlay = debug::DebugOverlayKind::player_controller_collision;
    collider.primitive = debug::DebugOverlayPrimitiveKind::wire_box;
    collider.position = center.value();
    collider.extent = {shape.width, shape.height, shape.width};
    collider.color = state.grounded ? debug::DebugOverlayColor{0.2F, 1.0F, 0.3F, 1.0F}
                                    : debug::DebugOverlayColor{0.2F, 0.5F, 1.0F, 1.0F};
    collider.label = std::string(player_controller_mode_name(state.mode));
    auto status = overlays.submit(std::move(collider));
    if (!status) {
        return status;
    }

    debug::DebugOverlayPrimitive velocity;
    velocity.overlay = debug::DebugOverlayKind::player_controller_probes;
    velocity.primitive = debug::DebugOverlayPrimitiveKind::line;
    velocity.position = center.value();
    velocity.extent = state.velocity * (1.0 / 60.0);
    velocity.color = {1.0F, 0.8F, 0.15F, 1.0F};
    velocity.label = "velocity";
    status = overlays.submit(std::move(velocity));
    if (!status) {
        return status;
    }

    debug::DebugOverlayPrimitive prediction;
    prediction.overlay = debug::DebugOverlayKind::movement_prediction;
    prediction.primitive = debug::DebugOverlayPrimitiveKind::sphere;
    prediction.position = state.position;
    prediction.extent = math::splat(prediction_error);
    prediction.color = prediction_error > 0.1
                           ? debug::DebugOverlayColor{1.0F, 0.15F, 0.1F, 1.0F}
                           : debug::DebugOverlayColor{0.2F, 1.0F, 0.3F, 1.0F};
    prediction.label = "prediction_error";
    return overlays.submit(std::move(prediction));
}

} // namespace heartstead::movement
