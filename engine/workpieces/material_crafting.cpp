#include "engine/workpieces/material_crafting.hpp"

#include "engine/core/hash.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace heartstead::workpieces {

core::Status ClayObjectRecord::validate() const {
    if (!object_id.is_valid() || !source_workpiece_id.is_valid() || !material_id.is_valid()) {
        return core::Status::failure(
            "clay.invalid_identity",
            "clay object requires stable object, workpiece, and material ids");
    }
    if (metadata.mass_units == 0) {
        return core::Status::failure("clay.invalid_mass", "clay object mass must be non-zero");
    }
    return core::Status::ok();
}

core::Status FiringOutcomeTable::validate() const {
    if (bands.empty())
        return core::Status::failure("firing.empty_table", "firing outcome table is empty");
    std::uint16_t previous = 0;
    for (std::size_t index = 0; index < bands.size(); ++index) {
        if (bands[index].maximum_risk_per_mille > 1000 ||
            (index > 0 && bands[index].maximum_risk_per_mille <= previous)) {
            return core::Status::failure("firing.invalid_band",
                                         "firing outcome thresholds must increase through 1000");
        }
        previous = bands[index].maximum_risk_per_mille;
    }
    return previous == 1000
               ? core::Status::ok()
               : core::Status::failure("firing.incomplete_table",
                                       "firing outcome table must cover risk through 1000");
}

core::Result<FiringOutcome> FiringOutcomeTable::resolve(std::uint16_t structural_risk_per_mille,
                                                        std::uint64_t deterministic_seed) const {
    auto status = validate();
    if (!status)
        return core::Result<FiringOutcome>::failure(status.error().code, status.error().message);
    if (structural_risk_per_mille > 1000) {
        return core::Result<FiringOutcome>::failure("firing.invalid_risk",
                                                    "firing risk must be 0..1000");
    }
    core::StableHash64 hash;
    hash.add_u64_le(deterministic_seed);
    hash.add_u64_le(structural_risk_per_mille);
    const auto sampled =
        static_cast<std::uint16_t>((hash.value() + structural_risk_per_mille) % 1001U);
    for (const auto& band : bands) {
        if (sampled <= band.maximum_risk_per_mille)
            return core::Result<FiringOutcome>::success(band.outcome);
    }
    return core::Result<FiringOutcome>::success(FiringOutcome::shattered);
}

core::Status PitFiringRecord::validate() const {
    if (!station_id.is_valid() || !position.is_valid() || ware_ids.empty()) {
        return core::Status::failure("pit_firing.invalid_record",
                                     "pit firing requires a station, position, and stacked wares");
    }
    std::set<std::uint64_t> ids;
    for (const auto ware : ware_ids) {
        if (!ware.is_valid() || !ids.insert(ware.value()).second) {
            return core::Status::failure("pit_firing.invalid_ware",
                                         "pit firing ware ids must be unique and valid");
        }
    }
    if (ignited_at > 0 && fuel_ticks == 0) {
        return core::Status::failure("pit_firing.missing_fuel", "ignited pit firing requires fuel");
    }
    return core::Status::ok();
}

bool PitFiringRecord::cooling(simulation::WorldTick world_time) const noexcept {
    return ignited_at > 0 && world_time < cooldown_until;
}

core::Status MoltenItemRecord::validate() const {
    if (!item_id.is_valid() || !material_id.is_valid() || material_units == 0 ||
        !position.is_valid()) {
        return core::Status::failure(
            "casting.invalid_molten_item",
            "molten item identity, material, amount, and position are required");
    }
    if (state == MoltenState::held_for_pour && (!held_by.is_valid() || pour_window_ends_at == 0)) {
        return core::Status::failure("casting.invalid_held_state",
                                     "held molten item requires a player and pour deadline");
    }
    if (state != MoltenState::held_for_pour && (held_by.is_valid() || pour_window_ends_at != 0))
        return core::Status::failure("casting.invalid_unheld_state",
                                     "only held molten items may retain holder or pour deadline");
    return core::Status::ok();
}

core::Status MouldRecord::validate() const {
    if (!mould_id.is_valid() || !pattern_id.is_valid() || !position.is_valid()) {
        return core::Status::failure("casting.invalid_mould",
                                     "mould identity, pattern, and position are required");
    }
    if (career == MouldCareer::redware_single_use && uses_remaining > 1) {
        return core::Status::failure("casting.invalid_mould_career",
                                     "redware mould cannot have more than one use");
    }
    if (contained_material_units == 0 && cooling_until != 0)
        return core::Status::failure("casting.invalid_mould_cooling",
                                     "empty mould cannot retain a cooling deadline");
    return core::Status::ok();
}

core::Status CastingRuntime::draw_for_pour(MoltenItemRecord& molten, core::NetId player,
                                           simulation::WorldTick world_time,
                                           simulation::WorldTick pour_window_ticks) {
    auto status = molten.validate();
    if (!status)
        return status;
    if (molten.state != MoltenState::molten || !player.is_valid() || pour_window_ticks == 0 ||
        world_time > std::numeric_limits<simulation::WorldTick>::max() - pour_window_ticks) {
        return core::Status::failure("casting.cannot_draw",
                                     "molten item cannot enter a held pour window");
    }
    auto staged = molten;
    staged.state = MoltenState::held_for_pour;
    staged.held_by = player;
    staged.pour_window_ends_at = world_time + pour_window_ticks;
    status = staged.validate();
    if (status)
        molten = staged;
    return status;
}

core::Status CastingRuntime::pour(MoltenItemRecord& molten, MouldRecord& mould, core::NetId player,
                                  simulation::WorldTick world_time,
                                  simulation::WorldTick cooling_ticks,
                                  double maximum_distance_blocks) {
    auto status = molten.validate();
    if (!status)
        return status;
    status = mould.validate();
    if (!status)
        return status;
    if (molten.state != MoltenState::held_for_pour || molten.held_by != player ||
        world_time > molten.pour_window_ends_at || cooling_ticks == 0 ||
        mould.contained_material_units != 0 || mould.cooling_until > world_time ||
        mould.uses_remaining == 0 || !std::isfinite(maximum_distance_blocks) ||
        maximum_distance_blocks <= 0.0) {
        return core::Status::failure("casting.invalid_pour",
                                     "pour window or mould state is invalid");
    }
    const auto relative = molten.position.relative_to(mould.position.anchor);
    const auto mould_local = mould.position.local_offset;
    const auto dx = relative.x - mould_local.x;
    const auto dy = relative.y - mould_local.y;
    const auto dz = relative.z - mould_local.z;
    if (dx * dx + dy * dy + dz * dz > maximum_distance_blocks * maximum_distance_blocks) {
        return core::Status::failure("casting.mould_out_of_range",
                                     "mould is outside the pour interaction range");
    }
    if (world_time > std::numeric_limits<simulation::WorldTick>::max() - cooling_ticks) {
        return core::Status::failure("casting.cooling_overflow",
                                     "mould cooling deadline overflows world time");
    }
    auto staged_molten = molten;
    auto staged_mould = mould;
    staged_mould.contained_material_units = staged_molten.material_units;
    staged_mould.cooling_until = world_time + cooling_ticks;
    staged_molten.state = MoltenState::poured;
    staged_molten.held_by = {};
    staged_molten.pour_window_ends_at = 0;
    if (staged_mould.career == MouldCareer::redware_single_use)
        --staged_mould.uses_remaining;
    status = staged_molten.validate();
    if (!status)
        return status;
    status = staged_mould.validate();
    if (!status)
        return status;
    molten = staged_molten;
    mould = staged_mould;
    return core::Status::ok();
}

core::Result<std::uint64_t> CastingRuntime::recycle_units(std::uint64_t original_units,
                                                          ToolWearBand wear) {
    if (original_units == 0) {
        return core::Result<std::uint64_t>::failure("casting.invalid_recycle_units",
                                                    "recycling requires material units");
    }
    switch (wear) {
    case ToolWearBand::fresh:
    case ToolWearBand::lightly_used:
        return core::Result<std::uint64_t>::success(original_units);
    case ToolWearBand::worn:
        return core::Result<std::uint64_t>::success((original_units / 4U) * 3U +
                                                    ((original_units % 4U) * 3U) / 4U);
    case ToolWearBand::exhausted:
        return core::Result<std::uint64_t>::success(original_units / 2U);
    }
    return core::Result<std::uint64_t>::failure("casting.invalid_wear",
                                                "tool wear band is invalid");
}

std::string_view clay_object_state_name(ClayObjectState state) noexcept {
    switch (state) {
    case ClayObjectState::green:
        return "green";
    case ClayObjectState::dry:
        return "dry";
    case ClayObjectState::fired:
        return "fired";
    case ClayObjectState::crazed:
        return "crazed";
    case ClayObjectState::cracked:
        return "cracked";
    case ClayObjectState::shattered:
        return "shattered";
    }
    return "unknown";
}

std::string_view firing_outcome_name(FiringOutcome outcome) noexcept {
    switch (outcome) {
    case FiringOutcome::fired:
        return "fired";
    case FiringOutcome::crazed:
        return "crazed";
    case FiringOutcome::cracked:
        return "cracked";
    case FiringOutcome::shattered:
        return "shattered";
    }
    return "unknown";
}

} // namespace heartstead::workpieces
