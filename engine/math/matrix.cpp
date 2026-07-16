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

Mat4f rotation_x_matrix(float radians) noexcept {
    if (!std::isfinite(radians)) {
        return {};
    }
    auto result = Mat4f::identity();
    const auto cosine = std::cos(radians);
    const auto sine = std::sin(radians);
    result.at(1, 1) = cosine;
    result.at(1, 2) = -sine;
    result.at(2, 1) = sine;
    result.at(2, 2) = cosine;
    return result;
}

Mat4f rotation_y_matrix(float radians) noexcept {
    if (!std::isfinite(radians)) {
        return {};
    }
    auto result = Mat4f::identity();
    const auto cosine = std::cos(radians);
    const auto sine = std::sin(radians);
    result.at(0, 0) = cosine;
    result.at(0, 2) = sine;
    result.at(2, 0) = -sine;
    result.at(2, 2) = cosine;
    return result;
}

Mat4f rotation_z_matrix(float radians) noexcept {
    if (!std::isfinite(radians)) {
        return {};
    }
    auto result = Mat4f::identity();
    const auto cosine = std::cos(radians);
    const auto sine = std::sin(radians);
    result.at(0, 0) = cosine;
    result.at(0, 1) = -sine;
    result.at(1, 0) = sine;
    result.at(1, 1) = cosine;
    return result;
}

Mat4f transform_matrix(const Transform3f& transform) noexcept {
    if (!transform.is_finite() || !transform.has_non_zero_scale()) {
        return {};
    }
    constexpr auto degrees_to_radians = std::numbers::pi_v<float> / 180.0F;
    const auto rotation = rotation_z_matrix(transform.rotation_degrees.z * degrees_to_radians) *
                          rotation_y_matrix(transform.rotation_degrees.y * degrees_to_radians) *
                          rotation_x_matrix(transform.rotation_degrees.x * degrees_to_radians);
    return translation_matrix(transform.position) * rotation * scale_matrix(transform.scale);
}

Bounds3f transform_bounds(const Mat4f& transform, const Bounds3f& bounds) noexcept {
    if (!transform.is_finite() || !bounds.is_valid()) {
        return {};
    }
    Bounds3f result;
    bool first = true;
    for (std::size_t corner = 0; corner < 8; ++corner) {
        const Vec4f point{
            (corner & 1U) != 0 ? bounds.max.x : bounds.min.x,
            (corner & 2U) != 0 ? bounds.max.y : bounds.min.y,
            (corner & 4U) != 0 ? bounds.max.z : bounds.min.z,
            1.0F,
        };
        const auto transformed = transform * point;
        if (!transformed.is_finite() || transformed.w == 0.0F) {
            return {};
        }
        const Vec3f value{transformed.x / transformed.w, transformed.y / transformed.w,
                          transformed.z / transformed.w};
        if (first) {
            result = {value, value};
            first = false;
        } else {
            result.min = component_min(result.min, value);
            result.max = component_max(result.max, value);
        }
    }
    return result;
}

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
