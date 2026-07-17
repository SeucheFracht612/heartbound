#include "engine/movement/carried_load.hpp"

#include <algorithm>
#include <limits>
#include <ranges>

namespace heartstead::movement {

namespace {

[[nodiscard]] bool checked_add(std::uint64_t& total, std::uint64_t value) noexcept {
    if (value > std::numeric_limits<std::uint64_t>::max() - total) {
        return false;
    }
    total += value;
    return true;
}

} // namespace

core::Result<CarriedLoadSummary>
summarize_carried_load(std::span<const items::ItemStack> stacks,
                       std::span<const items::ItemDefinition> definitions,
                       std::span<const cargo::CargoRecord> carried_cargo,
                       std::uint64_t capacity_grams) {
    if (capacity_grams == 0) {
        return core::Result<CarriedLoadSummary>::failure("carried_load.invalid_capacity",
                                                         "carried load capacity must be non-zero");
    }
    CarriedLoadSummary result;
    result.capacity_grams = capacity_grams;
    for (const auto& stack : stacks) {
        const auto found = std::ranges::find(definitions, stack.prototype_id,
                                             &items::ItemDefinition::prototype_id);
        if (found == definitions.end()) {
            return core::Result<CarriedLoadSummary>::failure(
                "carried_load.missing_item_definition",
                "carried item has no loaded definition: " + stack.prototype_id.value());
        }
        if (stack.count > 0 && found->mass_grams >
                                   std::numeric_limits<std::uint64_t>::max() / stack.count) {
            return core::Result<CarriedLoadSummary>::failure("carried_load.mass_overflow",
                                                             "carried item mass overflows");
        }
        if (!checked_add(result.item_mass_grams, found->mass_grams * stack.count)) {
            return core::Result<CarriedLoadSummary>::failure("carried_load.mass_overflow",
                                                             "carried item mass overflows");
        }
    }
    for (const auto& cargo_record : carried_cargo) {
        if (!checked_add(result.cargo_mass_grams, cargo_record.mass_grams)) {
            return core::Result<CarriedLoadSummary>::failure("carried_load.mass_overflow",
                                                             "carried cargo mass overflows");
        }
    }
    result.total_mass_grams = result.item_mass_grams;
    if (!checked_add(result.total_mass_grams, result.cargo_mass_grams)) {
        return core::Result<CarriedLoadSummary>::failure("carried_load.mass_overflow",
                                                         "total carried mass overflows");
    }
    if (result.total_mass_grams > std::numeric_limits<std::uint64_t>::max() / 1000) {
        result.load_per_mille = std::numeric_limits<std::uint32_t>::max();
    } else {
        const auto ratio = result.total_mass_grams * 1000 / capacity_grams;
        result.load_per_mille = static_cast<std::uint32_t>(
            std::min<std::uint64_t>(ratio, std::numeric_limits<std::uint32_t>::max()));
    }
    return core::Result<CarriedLoadSummary>::success(result);
}

} // namespace heartstead::movement
