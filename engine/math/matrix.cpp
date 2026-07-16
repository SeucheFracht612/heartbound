#include "engine/math/matrix.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace heartstead::math {

namespace {

[[nodiscard]] Vec3f normalized(Vec3f value) noexcept {
    const auto magnitude = static_cast<float>(length(value));
    if (!std::isfinite(magnitude) || magnitude <= 0.0F) {
        return {};
    }
    return value / magnitude;
}

} // namespace

bool Mat4f::is_finite() const noexcept {
    return std::ranges::all_of(elements, [](float value) { return std::isfinite(value); });
}

bool Vec4f::is_finite() const noexcept {
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(z) && std::isfinite(w);
}

Mat4f perspective_projection(float vertical_fov_radians, float aspect_ratio, float near_plane,
                             float far_plane) noexcept {
    Mat4f result;
    if (!std::isfinite(vertical_fov_radians) || !std::isfinite(aspect_ratio) ||
        !std::isfinite(near_plane) || !std::isfinite(far_plane) || aspect_ratio <= 0.0F ||
        near_plane <= 0.0F || far_plane <= near_plane || vertical_fov_radians <= 0.0F ||
        vertical_fov_radians >= std::numbers::pi_v<float>) {
        return result;
    }

    const auto focal_length = 1.0F / std::tan(vertical_fov_radians * 0.5F);
    result.at(0, 0) = focal_length / aspect_ratio;
    result.at(1, 1) = -focal_length;
    result.at(2, 2) = far_plane / (near_plane - far_plane);
    result.at(2, 3) = (far_plane * near_plane) / (near_plane - far_plane);
    result.at(3, 2) = -1.0F;
    return result;
}

Vec3f camera_forward(float yaw_radians, float pitch_radians) noexcept {
    const auto cos_pitch = std::cos(pitch_radians);
    return normalized({cos_pitch * std::sin(yaw_radians), std::sin(pitch_radians),
                       -cos_pitch * std::cos(yaw_radians)});
}

Vec3f camera_right(float yaw_radians) noexcept {
    return normalized({std::cos(yaw_radians), 0.0F, std::sin(yaw_radians)});
}

Mat4f view_matrix(Vec3f position, float yaw_radians, float pitch_radians) noexcept {
    const auto forward = camera_forward(yaw_radians, pitch_radians);
    const auto right = normalized(cross(forward, Vec3f{0.0F, 1.0F, 0.0F}));
    const auto up = cross(right, forward);

    auto result = Mat4f::identity();
    result.at(0, 0) = right.x;
    result.at(0, 1) = right.y;
    result.at(0, 2) = right.z;
    result.at(0, 3) = -dot(right, position);
    result.at(1, 0) = up.x;
    result.at(1, 1) = up.y;
    result.at(1, 2) = up.z;
    result.at(1, 3) = -dot(up, position);
    result.at(2, 0) = -forward.x;
    result.at(2, 1) = -forward.y;
    result.at(2, 2) = -forward.z;
    result.at(2, 3) = dot(forward, position);
    return result;
}

} // namespace heartstead::math
