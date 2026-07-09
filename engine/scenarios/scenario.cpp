#include "engine/scenarios/scenario.hpp"

#include <string>

namespace heartstead::scenarios {

core::Status ScenarioDefinition::validate() const {
    if (!prototype_id.is_valid()) {
        return core::Status::failure("scenario_definition.invalid_prototype",
                                     "scenario definition prototype id must be valid");
    }
    if (!core::is_valid_local_id(start_region)) {
        return core::Status::failure("scenario_definition.invalid_start_region",
                                     "scenario start region must be a valid local id");
    }
    for (const auto& item : starting_items) {
        if (!item.is_valid()) {
            return core::Status::failure("scenario_definition.invalid_starting_item",
                                         "scenario starting item id must be valid");
        }
    }
    for (const auto& cargo : starting_cargo) {
        if (!cargo.is_valid()) {
            return core::Status::failure("scenario_definition.invalid_starting_cargo",
                                         "scenario starting cargo id must be valid");
        }
    }
    for (const auto& tag : tags) {
        if (!core::is_valid_local_id(tag)) {
            return core::Status::failure("scenario_definition.invalid_tag",
                                         "scenario tag is invalid: " + tag);
        }
    }
    return core::Status::ok();
}

core::Result<ScenarioSpawnMode> scenario_spawn_mode_from_name(std::string_view name) {
    if (name == "homestead") {
        return core::Result<ScenarioSpawnMode>::success(ScenarioSpawnMode::homestead);
    }
    if (name == "outpost") {
        return core::Result<ScenarioSpawnMode>::success(ScenarioSpawnMode::outpost);
    }
    if (name == "debug") {
        return core::Result<ScenarioSpawnMode>::success(ScenarioSpawnMode::debug);
    }
    return core::Result<ScenarioSpawnMode>::failure(
        "scenario.invalid_spawn_mode", "unsupported scenario spawn mode: " + std::string(name));
}

std::string_view scenario_spawn_mode_name(ScenarioSpawnMode mode) noexcept {
    switch (mode) {
    case ScenarioSpawnMode::homestead:
        return "homestead";
    case ScenarioSpawnMode::outpost:
        return "outpost";
    case ScenarioSpawnMode::debug:
        return "debug";
    }
    return "unknown";
}

} // namespace heartstead::scenarios
