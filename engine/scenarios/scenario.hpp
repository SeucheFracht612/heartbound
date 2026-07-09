#pragma once

#include "engine/core/ids.hpp"
#include "engine/core/result.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace heartstead::scenarios {

enum class ScenarioSpawnMode {
    homestead,
    outpost,
    debug,
};

struct ScenarioDefinition {
    core::PrototypeId prototype_id;
    std::string start_region;
    ScenarioSpawnMode spawn_mode = ScenarioSpawnMode::homestead;
    std::vector<core::PrototypeId> starting_items;
    std::vector<core::PrototypeId> starting_cargo;
    std::vector<std::string> tags;

    [[nodiscard]] core::Status validate() const;
};

[[nodiscard]] core::Result<ScenarioSpawnMode> scenario_spawn_mode_from_name(std::string_view name);
[[nodiscard]] std::string_view scenario_spawn_mode_name(ScenarioSpawnMode mode) noexcept;

} // namespace heartstead::scenarios
