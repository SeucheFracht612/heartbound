#pragma once

#include "engine/core/result.hpp"
#include "engine/math/matrix.hpp"
#include "engine/movement/player_controller.hpp"
#include "engine/world/coords/world_position.hpp"

namespace heartstead::movement {

enum class PlayerCameraPerspective : std::uint8_t { first_person, third_person };

struct PlayerCameraConfig {
    double standing_eye_height = 1.62;
    double crouch_eye_height = 1.05;
    double roll_eye_height = 0.72;
    double third_person_distance = 4.0;
    double third_person_height = 0.45;
    double third_person_shoulder = 0.35;
    float vertical_fov_degrees = 80.0F;
    float near_plane = 0.05F;
    float far_plane = 2048.0F;

    [[nodiscard]] core::Status validate() const;
};

struct PlayerBodyRigPose {
    world::WorldPosition root_position;
    double yaw_degrees = 0.0;
    math::Vec3d planar_velocity{};
    bool crouched = false;
    bool rolling = false;
    bool local_body_visible = false;
};

struct PlayerCameraFrame {
    PlayerCameraPerspective perspective = PlayerCameraPerspective::first_person;
    world::WorldPosition position;
    math::Vec3d forward{0.0, 0.0, 1.0};
    world::FloatingOrigin floating_origin;
    math::Mat4f view;
    math::Mat4f projection;
    math::Mat4f view_projection;
    PlayerBodyRigPose body;
};

class PlayerCameraRig {
  public:
    explicit PlayerCameraRig(PlayerCameraConfig config = {});

    [[nodiscard]] core::Result<PlayerCameraFrame>
    evaluate(const PlayerControllerState& player, PlayerCameraPerspective perspective,
             std::uint32_t viewport_width, std::uint32_t viewport_height) const;

  private:
    PlayerCameraConfig config_;
};

} // namespace heartstead::movement
