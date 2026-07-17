#include "engine/movement/player_camera.hpp"

#include <cmath>

namespace heartstead::movement {

core::Status PlayerCameraConfig::validate() const {
    if (!std::isfinite(standing_eye_height) || !std::isfinite(crouch_eye_height) ||
        !std::isfinite(roll_eye_height) || !std::isfinite(third_person_distance) ||
        !std::isfinite(third_person_height) || !std::isfinite(third_person_shoulder) ||
        standing_eye_height <= 0.0 || crouch_eye_height <= 0.0 || roll_eye_height <= 0.0 ||
        third_person_distance <= 0.0 || crouch_eye_height > standing_eye_height ||
        roll_eye_height > crouch_eye_height) {
        return core::Status::failure("player_camera.invalid_config",
                                     "player camera dimensions are invalid");
    }
    if (!std::isfinite(vertical_fov_degrees) || !std::isfinite(near_plane) ||
        !std::isfinite(far_plane) || vertical_fov_degrees <= 0.0F ||
        vertical_fov_degrees >= 180.0F || near_plane <= 0.0F || far_plane <= near_plane) {
        return core::Status::failure("player_camera.invalid_projection",
                                     "player camera projection values are invalid");
    }
    return core::Status::ok();
}

PlayerCameraRig::PlayerCameraRig(PlayerCameraConfig config) : config_(config) {}

core::Result<PlayerCameraFrame>
PlayerCameraRig::evaluate(const PlayerControllerState& player,
                          PlayerCameraPerspective perspective, std::uint32_t viewport_width,
                          std::uint32_t viewport_height) const {
    auto config_status = config_.validate();
    if (!config_status || !player.position.is_valid() || viewport_width == 0 ||
        viewport_height == 0) {
        return core::Result<PlayerCameraFrame>::failure(
            !config_status ? config_status.error().code : "player_camera.invalid_viewport",
            !config_status ? config_status.error().message
                           : "player camera viewport and player position must be valid");
    }
    constexpr double degrees_to_radians = 3.14159265358979323846 / 180.0;
    const auto yaw = static_cast<double>(player.yaw_centidegrees) * 0.01 * degrees_to_radians;
    const auto pitch = static_cast<double>(player.pitch_centidegrees) * 0.01 * degrees_to_radians;
    const math::Vec3d forward{std::sin(yaw) * std::cos(pitch), std::sin(pitch),
                              std::cos(yaw) * std::cos(pitch)};
    const math::Vec3d right{std::cos(yaw), 0.0, -std::sin(yaw)};
    const auto eye_height = player.mode == PlayerControllerMode::rolling
                                ? config_.roll_eye_height
                                : player.crouched ? config_.crouch_eye_height
                                                   : config_.standing_eye_height;
    auto camera_offset = math::Vec3d{0.0, eye_height, 0.0};
    if (perspective == PlayerCameraPerspective::third_person) {
        camera_offset += forward * -config_.third_person_distance;
        camera_offset += right * config_.third_person_shoulder;
        camera_offset.y += config_.third_person_height;
    }
    auto camera_position = world::WorldPosition::from_anchor(
        player.position.anchor, player.position.local_offset + camera_offset);
    if (!camera_position) {
        return core::Result<PlayerCameraFrame>::failure(camera_position.error().code,
                                                        camera_position.error().message);
    }

    PlayerCameraFrame frame;
    frame.perspective = perspective;
    frame.position = camera_position.value();
    frame.forward = forward;
    frame.floating_origin.block = camera_position.value().anchor;
    const math::Vec3f local_eye{static_cast<float>(camera_position.value().local_offset.x),
                                static_cast<float>(camera_position.value().local_offset.y),
                                static_cast<float>(camera_position.value().local_offset.z)};
    constexpr float pi = 3.14159265358979323846F;
    frame.view = math::view_matrix(local_eye, pi - static_cast<float>(yaw),
                                   static_cast<float>(pitch));
    frame.projection = math::perspective_projection(
        config_.vertical_fov_degrees * pi / 180.0F,
        static_cast<float>(viewport_width) / static_cast<float>(viewport_height),
        config_.near_plane, config_.far_plane);
    if (!frame.view.is_finite() || !frame.projection.is_finite()) {
        return core::Result<PlayerCameraFrame>::failure(
            "player_camera.invalid_matrix", "player camera produced a non-finite matrix");
    }
    frame.view_projection = frame.projection * frame.view;
    frame.body.root_position = player.position;
    frame.body.yaw_degrees = static_cast<double>(player.yaw_centidegrees) * 0.01;
    frame.body.planar_velocity = {player.velocity.x, 0.0, player.velocity.z};
    frame.body.crouched = player.crouched;
    frame.body.rolling = player.mode == PlayerControllerMode::rolling;
    frame.body.local_body_visible = perspective == PlayerCameraPerspective::third_person;
    return core::Result<PlayerCameraFrame>::success(frame);
}

} // namespace heartstead::movement
