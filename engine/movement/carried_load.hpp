#pragma once

#include "engine/cargo/cargo.hpp"
#include "engine/core/result.hpp"
#include "engine/items/item_stack.hpp"

#include <cstdint>
#include <span>

namespace heartstead::movement {

struct CarriedLoadSummary {
    std::uint64_t item_mass_grams = 0;
    std::uint64_t cargo_mass_grams = 0;
    std::uint64_t total_mass_grams = 0;
    std::uint64_t capacity_grams = 0;
    std::uint32_t load_per_mille = 0;
};

[[nodiscard]] core::Result<CarriedLoadSummary>
summarize_carried_load(std::span<const items::ItemStack> stacks,
                       std::span<const items::ItemDefinition> definitions,
                       std::span<const cargo::CargoRecord> carried_cargo,
                       std::uint64_t capacity_grams);

} // namespace heartstead::movement
