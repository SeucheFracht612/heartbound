#pragma once

#include "engine/modding/generic_prototype.hpp"
#include "engine/scenarios/scenario.hpp"

namespace heartstead::scenarios {

[[nodiscard]] core::Result<ScenarioDefinition>
scenario_definition_from_prototype(const modding::GenericPrototype& prototype);

} // namespace heartstead::scenarios
