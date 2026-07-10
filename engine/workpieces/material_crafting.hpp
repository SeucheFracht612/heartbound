#pragma once

#include "engine/core/ids.hpp"
#include "engine/core/result.hpp"
#include "engine/simulation/world_time.hpp"
#include "engine/workpieces/workpiece_state.hpp"
#include "engine/world/coords/world_position.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace heartstead::workpieces {

enum class ClayObjectState : std::uint8_t { green, dry, fired, crazed, cracked, shattered };
enum class FiringOutcome : std::uint8_t { fired, crazed, cracked, shattered };

struct ClayObjectRecord {
    core::SaveId object_id;
    core::WorkpieceId source_workpiece_id;
    core::PrototypeId material_id;
    WorkpieceOutputMetadata metadata;
    ClayObjectState state = ClayObjectState::green;
    simulation::WorldTick state_changed_at = 0;
    std::uint32_t grog_return_units = 0;

    [[nodiscard]] core::Status validate() const;
};

struct FiringOutcomeBand {
    std::uint16_t maximum_risk_per_mille = 1000;
    FiringOutcome outcome = FiringOutcome::fired;
};

struct FiringOutcomeTable {
    std::vector<FiringOutcomeBand> bands;

    [[nodiscard]] core::Status validate() const;
    [[nodiscard]] core::Result<FiringOutcome> resolve(std::uint16_t structural_risk_per_mille,
                                                      std::uint64_t deterministic_seed) const;
};

struct PitFiringRecord {
    core::SaveId station_id;
    world::WorldPosition position;
    std::vector<core::SaveId> ware_ids;
    std::uint64_t fuel_ticks = 0;
    simulation::WorldTick ignited_at = 0;
    simulation::WorldTick cooldown_until = 0;

    [[nodiscard]] core::Status validate() const;
    [[nodiscard]] bool cooling(simulation::WorldTick world_time) const noexcept;
};

enum class MoltenState : std::uint8_t { solid, melting, molten, held_for_pour, poured, cooled };
enum class MouldCareer : std::uint8_t { redware_single_use, soapstone_reusable };
enum class ToolWearBand : std::uint8_t { fresh, lightly_used, worn, exhausted };

struct MoltenItemRecord {
    core::SaveId item_id;
    core::PrototypeId material_id;
    std::uint64_t material_units = 0;
    MoltenState state = MoltenState::solid;
    simulation::WorldTick pour_window_ends_at = 0;
    core::NetId held_by;
    world::WorldPosition position;

    [[nodiscard]] core::Status validate() const;
};

struct MouldRecord {
    core::SaveId mould_id;
    core::PrototypeId pattern_id;
    MouldCareer career = MouldCareer::redware_single_use;
    std::uint32_t uses_remaining = 1;
    world::WorldPosition position;
    simulation::WorldTick cooling_until = 0;
    std::uint64_t contained_material_units = 0;

    [[nodiscard]] core::Status validate() const;
};

class CastingRuntime {
  public:
    [[nodiscard]] static core::Status draw_for_pour(MoltenItemRecord& molten, core::NetId player,
                                                    simulation::WorldTick world_time,
                                                    simulation::WorldTick pour_window_ticks);
    [[nodiscard]] static core::Status pour(MoltenItemRecord& molten, MouldRecord& mould,
                                           core::NetId player, simulation::WorldTick world_time,
                                           simulation::WorldTick cooling_ticks,
                                           double maximum_distance_blocks = 4.0);
    [[nodiscard]] static core::Result<std::uint64_t> recycle_units(std::uint64_t original_units,
                                                                   ToolWearBand wear);
};

[[nodiscard]] std::string_view clay_object_state_name(ClayObjectState state) noexcept;
[[nodiscard]] std::string_view firing_outcome_name(FiringOutcome outcome) noexcept;

} // namespace heartstead::workpieces
