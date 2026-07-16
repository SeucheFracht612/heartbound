#pragma once

#include "engine/math/matrix.hpp"
#include "engine/math/vector.hpp"

#include <array>

namespace heartstead::renderer {

struct FrustumPlane {
    math::Vec3f normal{};
    float distance = 0.0F;

    [[nodiscard]] bool is_valid() const noexcept;
    [[nodiscard]] float signed_distance(math::Vec3f point) const noexcept;
};

class RenderFrustum {
  public:
    // Extracts planes for column vectors and Vulkan's [0, 1] clip-space depth range.
    [[nodiscard]] static RenderFrustum from_view_projection(const math::Mat4f& matrix) noexcept;

    [[nodiscard]] bool is_valid() const noexcept;
    [[nodiscard]] bool intersects(const math::Bounds3f& bounds) const noexcept;
    [[nodiscard]] const std::array<FrustumPlane, 6>& planes() const noexcept;

  private:
    std::array<FrustumPlane, 6> planes_{};
    bool valid_ = false;
};

} // namespace heartstead::renderer
