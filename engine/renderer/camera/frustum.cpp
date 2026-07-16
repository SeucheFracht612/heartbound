#include "engine/renderer/camera/frustum.hpp"

#include <cmath>

namespace heartstead::renderer {

namespace {

[[nodiscard]] FrustumPlane normalized_plane(float x, float y, float z, float distance,
                                            bool& valid) noexcept {
    const auto magnitude = std::sqrt((x * x) + (y * y) + (z * z));
    if (!std::isfinite(magnitude) || magnitude <= 0.0F || !std::isfinite(distance)) {
        valid = false;
        return {};
    }
    const auto inverse = 1.0F / magnitude;
    return {{x * inverse, y * inverse, z * inverse}, distance * inverse};
}

} // namespace

bool FrustumPlane::is_valid() const noexcept {
    return normal.is_finite() && std::isfinite(distance) && math::length_squared(normal) > 0.0F;
}

float FrustumPlane::signed_distance(math::Vec3f point) const noexcept {
    return math::dot(normal, point) + distance;
}

RenderFrustum RenderFrustum::from_view_projection(const math::Mat4f& matrix) noexcept {
    RenderFrustum result;
    if (!matrix.is_finite()) {
        return result;
    }

    bool valid = true;
    const auto plane_from_rows = [&matrix, &valid](std::size_t first, float first_scale,
                                                   std::size_t second, float second_scale) {
        return normalized_plane(
            matrix.at(first, 0) * first_scale + matrix.at(second, 0) * second_scale,
            matrix.at(first, 1) * first_scale + matrix.at(second, 1) * second_scale,
            matrix.at(first, 2) * first_scale + matrix.at(second, 2) * second_scale,
            matrix.at(first, 3) * first_scale + matrix.at(second, 3) * second_scale, valid);
    };
    const auto plane_from_row = [&matrix, &valid](std::size_t row) {
        return normalized_plane(matrix.at(row, 0), matrix.at(row, 1), matrix.at(row, 2),
                                matrix.at(row, 3), valid);
    };

    result.planes_[0] = plane_from_rows(3, 1.0F, 0, 1.0F);  // left
    result.planes_[1] = plane_from_rows(3, 1.0F, 0, -1.0F); // right
    result.planes_[2] = plane_from_rows(3, 1.0F, 1, 1.0F);  // bottom
    result.planes_[3] = plane_from_rows(3, 1.0F, 1, -1.0F); // top
    result.planes_[4] = plane_from_row(2);                  // near: z >= 0
    result.planes_[5] = plane_from_rows(3, 1.0F, 2, -1.0F); // far: z <= w
    result.valid_ = valid;
    return result;
}

bool RenderFrustum::is_valid() const noexcept {
    return valid_;
}

bool RenderFrustum::intersects(const math::Bounds3f& bounds) const noexcept {
    // Invalid data stays visible while debugging instead of causing geometry to disappear.
    if (!valid_ || !bounds.is_valid()) {
        return true;
    }

    for (const auto& plane : planes_) {
        const math::Vec3f positive{
            plane.normal.x >= 0.0F ? bounds.max.x : bounds.min.x,
            plane.normal.y >= 0.0F ? bounds.max.y : bounds.min.y,
            plane.normal.z >= 0.0F ? bounds.max.z : bounds.min.z,
        };
        if (plane.signed_distance(positive) < 0.0F) {
            return false;
        }
    }
    return true;
}

const std::array<FrustumPlane, 6>& RenderFrustum::planes() const noexcept {
    return planes_;
}

} // namespace heartstead::renderer
