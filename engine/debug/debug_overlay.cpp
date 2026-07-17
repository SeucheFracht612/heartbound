#include "engine/debug/debug_overlay.hpp"

#include <cmath>

namespace heartstead::debug {

namespace {
[[nodiscard]] constexpr std::uint8_t key(DebugOverlayKind kind) noexcept {
    return static_cast<std::uint8_t>(kind);
}
} // namespace

core::Status DebugOverlayPrimitive::validate() const {
    const auto negative_extent = extent.x < 0.0 || extent.y < 0.0 || extent.z < 0.0;
    if (!position.is_valid() || !std::isfinite(extent.x) || !std::isfinite(extent.y) ||
        !std::isfinite(extent.z) ||
        (primitive != DebugOverlayPrimitiveKind::line && negative_extent)) {
        return core::Status::failure("debug_overlay.invalid_primitive",
                                     "debug overlay primitive has invalid world bounds");
    }
    const auto channel_valid = [](float value) { return value >= 0.0F && value <= 1.0F; };
    if (!channel_valid(color.red) || !channel_valid(color.green) || !channel_valid(color.blue) ||
        !channel_valid(color.alpha)) {
        return core::Status::failure("debug_overlay.invalid_color",
                                     "debug overlay color channels must be 0..1");
    }
    return core::Status::ok();
}

DebugOverlayRegistry::DebugOverlayRegistry() {
    for (std::uint8_t value = key(DebugOverlayKind::chunk_boundaries);
         value <= key(DebugOverlayKind::movement_prediction); ++value) {
        enabled_.emplace(value, false);
    }
}

void DebugOverlayRegistry::set_enabled(DebugOverlayKind kind, bool enabled) noexcept {
    enabled_[key(kind)] = enabled;
}

bool DebugOverlayRegistry::enabled(DebugOverlayKind kind) const noexcept {
    const auto found = enabled_.find(key(kind));
    return found != enabled_.end() && found->second;
}

core::Status DebugOverlayRegistry::submit(DebugOverlayPrimitive primitive) {
    auto status = primitive.validate();
    if (!status)
        return status;
    if (enabled(primitive.overlay))
        frame_.push_back(std::move(primitive));
    return core::Status::ok();
}

void DebugOverlayRegistry::clear_frame() noexcept {
    frame_.clear();
}

const std::vector<DebugOverlayPrimitive>& DebugOverlayRegistry::frame_primitives() const noexcept {
    return frame_;
}

std::vector<const DebugOverlayPrimitive*>
DebugOverlayRegistry::primitives(DebugOverlayKind kind) const {
    std::vector<const DebugOverlayPrimitive*> result;
    for (const auto& primitive : frame_) {
        if (primitive.overlay == kind)
            result.push_back(&primitive);
    }
    return result;
}

std::string_view debug_overlay_kind_name(DebugOverlayKind kind) noexcept {
    switch (kind) {
    case DebugOverlayKind::chunk_boundaries:
        return "chunk_boundaries";
    case DebugOverlayKind::chunk_coordinates:
        return "chunk_coordinates";
    case DebugOverlayKind::world_position_i64:
        return "world_position_i64";
    case DebugOverlayKind::block_render_bounds:
        return "block_render_bounds";
    case DebugOverlayKind::block_collision_bounds:
        return "block_collision_bounds";
    case DebugOverlayKind::mesh_dirty_regions:
        return "mesh_dirty_regions";
    case DebugOverlayKind::workpiece_grids:
        return "workpiece_grids";
    case DebugOverlayKind::assembly_parts_ports:
        return "assembly_parts_ports";
    case DebugOverlayKind::room_volumes:
        return "room_volumes";
    case DebugOverlayKind::light_fire_radius:
        return "light_fire_radius";
    case DebugOverlayKind::repel_radius:
        return "repel_radius";
    case DebugOverlayKind::process_states:
        return "process_states";
    case DebugOverlayKind::map_discovery_masks:
        return "map_discovery_masks";
    case DebugOverlayKind::network_interest:
        return "network_interest";
    case DebugOverlayKind::save_ids:
        return "save_ids";
    case DebugOverlayKind::server_logs_tail:
        return "server_logs_tail";
    case DebugOverlayKind::player_controller_collision:
        return "player_controller_collision";
    case DebugOverlayKind::player_controller_probes:
        return "player_controller_probes";
    case DebugOverlayKind::movement_prediction:
        return "movement_prediction";
    }
    return "unknown";
}

} // namespace heartstead::debug
