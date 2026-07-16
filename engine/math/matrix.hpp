#pragma once

#include "engine/math/vector.hpp"

#include <array>
#include <cstddef>

namespace heartstead::math {

// Matrices use column-major storage and multiply column vectors: clip = M * position.
// Element (row, column) is stored at column * 4 + row. This matches GLSL's default
// column-major matrix layout and lets Mat4f be copied directly into shader constants.
struct Mat4f {
    std::array<float, 16> elements{};

    [[nodiscard]] static constexpr Mat4f identity() noexcept {
        Mat4f result;
        result.at(0, 0) = 1.0F;
        result.at(1, 1) = 1.0F;
        result.at(2, 2) = 1.0F;
        result.at(3, 3) = 1.0F;
        return result;
    }

    [[nodiscard]] constexpr float& at(std::size_t row, std::size_t column) noexcept {
        return elements[column * 4 + row];
    }

    [[nodiscard]] constexpr float at(std::size_t row, std::size_t column) const noexcept {
        return elements[column * 4 + row];
    }

    [[nodiscard]] bool is_finite() const noexcept;

    friend constexpr bool operator==(const Mat4f&, const Mat4f&) = default;
};

struct Vec4f {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float w = 0.0F;

    [[nodiscard]] bool is_finite() const noexcept;

    friend constexpr bool operator==(const Vec4f&, const Vec4f&) = default;
};

[[nodiscard]] constexpr Mat4f operator*(const Mat4f& left, const Mat4f& right) noexcept {
    Mat4f result;
    for (std::size_t column = 0; column < 4; ++column) {
        for (std::size_t row = 0; row < 4; ++row) {
            float value = 0.0F;
            for (std::size_t inner = 0; inner < 4; ++inner) {
                value += left.at(row, inner) * right.at(inner, column);
            }
            result.at(row, column) = value;
        }
    }
    return result;
}

[[nodiscard]] constexpr Vec4f operator*(const Mat4f& matrix, Vec4f vector) noexcept {
    return {
        matrix.at(0, 0) * vector.x + matrix.at(0, 1) * vector.y +
            matrix.at(0, 2) * vector.z + matrix.at(0, 3) * vector.w,
        matrix.at(1, 0) * vector.x + matrix.at(1, 1) * vector.y +
            matrix.at(1, 2) * vector.z + matrix.at(1, 3) * vector.w,
        matrix.at(2, 0) * vector.x + matrix.at(2, 1) * vector.y +
            matrix.at(2, 2) * vector.z + matrix.at(2, 3) * vector.w,
        matrix.at(3, 0) * vector.x + matrix.at(3, 1) * vector.y +
            matrix.at(3, 2) * vector.z + matrix.at(3, 3) * vector.w,
    };
}

[[nodiscard]] constexpr Mat4f translation_matrix(Vec3f translation) noexcept {
    auto result = Mat4f::identity();
    result.at(0, 3) = translation.x;
    result.at(1, 3) = translation.y;
    result.at(2, 3) = translation.z;
    return result;
}

[[nodiscard]] constexpr Mat4f scale_matrix(Vec3f scale) noexcept {
    Mat4f result;
    result.at(0, 0) = scale.x;
    result.at(1, 1) = scale.y;
    result.at(2, 2) = scale.z;
    result.at(3, 3) = 1.0F;
    return result;
}

// Right-handed perspective with Vulkan's [0, 1] depth range. The Y row is flipped so a
// conventional positive-height Vulkan viewport preserves counter-clockwise front faces.
[[nodiscard]] Mat4f perspective_projection(float vertical_fov_radians, float aspect_ratio,
                                           float near_plane, float far_plane) noexcept;

// Right-handed view. yaw=0 and pitch=0 looks down -Z; +Y is world up.
[[nodiscard]] Mat4f view_matrix(Vec3f position, float yaw_radians,
                                float pitch_radians) noexcept;
[[nodiscard]] Vec3f camera_forward(float yaw_radians, float pitch_radians) noexcept;
[[nodiscard]] Vec3f camera_right(float yaw_radians) noexcept;

} // namespace heartstead::math
