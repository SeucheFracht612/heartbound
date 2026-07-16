#pragma once

#include "engine/core/result.hpp"

#include <cstdint>
#include <limits>

namespace heartstead::renderer {

// Chunk distances are cylindrical: horizontal radii apply in X/Z and vertical radii apply on Y.
// Each outer tier retains the inner tier, preventing camera motion from making required data
// unavailable to the next stage.
struct WorldRenderDistances {
    std::uint16_t simulation_radius = 8;

    std::uint16_t loaded_horizontal_radius = 24;
    std::uint16_t loaded_vertical_radius = 10;

    std::uint16_t mesh_horizontal_radius = 18;
    std::uint16_t mesh_vertical_radius = 7;

    std::uint16_t gpu_resident_horizontal_radius = 20;
    std::uint16_t gpu_resident_vertical_radius = 8;

    std::uint16_t visible_horizontal_radius = 16;
    std::uint16_t visible_vertical_radius = 6;

    std::uint16_t gpu_resident_hysteresis = 2;

    [[nodiscard]] core::Status validate() const noexcept {
        if (visible_horizontal_radius > mesh_horizontal_radius ||
            visible_vertical_radius > mesh_vertical_radius ||
            mesh_horizontal_radius > gpu_resident_horizontal_radius ||
            mesh_vertical_radius > gpu_resident_vertical_radius ||
            gpu_resident_horizontal_radius > loaded_horizontal_radius ||
            gpu_resident_vertical_radius > loaded_vertical_radius ||
            simulation_radius > loaded_horizontal_radius) {
            return core::Status::failure(
                "renderer.invalid_world_render_distances",
                "render distances must be ordered visible <= mesh <= GPU resident <= loaded");
        }
        const auto maximum =
            static_cast<std::uint32_t>(std::numeric_limits<std::uint16_t>::max());
        if (static_cast<std::uint32_t>(gpu_resident_horizontal_radius) +
                    gpu_resident_hysteresis >
                maximum ||
            static_cast<std::uint32_t>(gpu_resident_vertical_radius) +
                    gpu_resident_hysteresis >
                maximum) {
            return core::Status::failure(
                "renderer.invalid_gpu_residency_hysteresis",
                "GPU residency radius plus hysteresis must fit in uint16");
        }
        return core::Status::ok();
    }
};

} // namespace heartstead::renderer
