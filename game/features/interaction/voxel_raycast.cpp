#include "game/features/interaction/voxel_raycast.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace heartstead::game::interaction {

namespace {

struct AxisTraversal {
    std::int64_t step = 0;
    double next_distance = std::numeric_limits<double>::infinity();
    double distance_step = std::numeric_limits<double>::infinity();
};

[[nodiscard]] AxisTraversal traversal_axis(double local, double direction) noexcept {
    if (direction > 0.0) {
        return {1, (1.0 - local) / direction, 1.0 / direction};
    }
    if (direction < 0.0) {
        return {-1, local / -direction, 1.0 / -direction};
    }
    return {};
}

} // namespace

core::Result<VoxelRaycastResult> raycast_voxels(const world::ChunkDatabase& chunks,
                                                const VoxelRay& ray) {
    if (!ray.origin.is_valid() || !ray.direction.is_finite() ||
        !std::isfinite(ray.maximum_distance) || ray.maximum_distance <= 0.0) {
        return core::Result<VoxelRaycastResult>::failure(
            "voxel_raycast.invalid_ray", "voxel ray must have valid finite inputs");
    }
    const auto direction_length = math::length(ray.direction);
    if (!std::isfinite(direction_length) || direction_length <= 1.0e-12) {
        return core::Result<VoxelRaycastResult>::failure(
            "voxel_raycast.zero_direction", "voxel ray direction must be non-zero");
    }
    const auto direction = ray.direction / direction_length;
    auto x = traversal_axis(ray.origin.local_offset.x, direction.x);
    auto y = traversal_axis(ray.origin.local_offset.y, direction.y);
    auto z = traversal_axis(ray.origin.local_offset.z, direction.z);

    auto current = ray.origin.anchor;
    auto previous = current;
    math::Coord3i face_normal{};
    double distance = 0.0;
    for (;;) {
        const auto address = world::block_to_chunk_local(current);
        const auto* chunk = chunks.find(address.chunk);
        if (chunk == nullptr) {
            return core::Result<VoxelRaycastResult>::success({std::nullopt, true});
        }
        auto cell = chunk->get(address.local);
        if (!cell) {
            return core::Result<VoxelRaycastResult>::failure(cell.error().code,
                                                             cell.error().message);
        }
        if (!cell.value().is_air()) {
            return core::Result<VoxelRaycastResult>::success(
                {VoxelRaycastHit{current, previous, face_normal, cell.value(), distance}, false});
        }

        previous = current;
        world::BlockCoord step{};
        if (x.next_distance <= y.next_distance && x.next_distance <= z.next_distance) {
            distance = x.next_distance;
            x.next_distance += x.distance_step;
            step.x = x.step;
            face_normal = {static_cast<std::int32_t>(-x.step), 0, 0};
        } else if (y.next_distance <= z.next_distance) {
            distance = y.next_distance;
            y.next_distance += y.distance_step;
            step.y = y.step;
            face_normal = {0, static_cast<std::int32_t>(-y.step), 0};
        } else {
            distance = z.next_distance;
            z.next_distance += z.distance_step;
            step.z = z.step;
            face_normal = {0, 0, static_cast<std::int32_t>(-z.step)};
        }
        if (distance > ray.maximum_distance) {
            return core::Result<VoxelRaycastResult>::success({});
        }
        auto next = world::checked_block_coord_offset(current, step);
        if (!next) {
            return core::Result<VoxelRaycastResult>::failure(next.error().code,
                                                             next.error().message);
        }
        current = next.value();
    }
}

} // namespace heartstead::game::interaction
