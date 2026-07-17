#pragma once

#include "engine/core/result.hpp"
#include "engine/math/vector.hpp"
#include "engine/world/coords/world_position.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace heartstead::debug {

enum class DebugOverlayKind : std::uint8_t {
    chunk_boundaries,
    chunk_coordinates,
    world_position_i64,
    block_render_bounds,
    block_collision_bounds,
    mesh_dirty_regions,
    workpiece_grids,
    assembly_parts_ports,
    room_volumes,
    light_fire_radius,
    repel_radius,
    process_states,
    map_discovery_masks,
    network_interest,
    save_ids,
    server_logs_tail,
    player_controller_collision,
    player_controller_probes,
    movement_prediction,
};

enum class DebugOverlayPrimitiveKind : std::uint8_t { wire_box, line, sphere, text, cell_grid };

struct DebugOverlayColor {
    float red = 1.0F;
    float green = 1.0F;
    float blue = 1.0F;
    float alpha = 1.0F;
};

struct DebugOverlayPrimitive {
    DebugOverlayKind overlay = DebugOverlayKind::chunk_boundaries;
    DebugOverlayPrimitiveKind primitive = DebugOverlayPrimitiveKind::wire_box;
    world::WorldPosition position;
    math::Vec3d extent{1.0, 1.0, 1.0};
    DebugOverlayColor color;
    std::string label;
    std::uint64_t source_revision = 0;

    [[nodiscard]] core::Status validate() const;
};

class DebugOverlayRegistry {
  public:
    DebugOverlayRegistry();

    void set_enabled(DebugOverlayKind kind, bool enabled) noexcept;
    [[nodiscard]] bool enabled(DebugOverlayKind kind) const noexcept;
    [[nodiscard]] core::Status submit(DebugOverlayPrimitive primitive);
    void clear_frame() noexcept;
    [[nodiscard]] const std::vector<DebugOverlayPrimitive>& frame_primitives() const noexcept;
    [[nodiscard]] std::vector<const DebugOverlayPrimitive*> primitives(DebugOverlayKind kind) const;

  private:
    std::unordered_map<std::uint8_t, bool> enabled_;
    std::vector<DebugOverlayPrimitive> frame_;
};

[[nodiscard]] std::string_view debug_overlay_kind_name(DebugOverlayKind kind) noexcept;

} // namespace heartstead::debug
