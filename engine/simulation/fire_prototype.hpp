#pragma once

#include "engine/modding/generic_prototype.hpp"
#include "engine/simulation/fire.hpp"

namespace heartstead::simulation {

[[nodiscard]] core::Result<FireDefinition>
fire_definition_from_prototype(const modding::GenericPrototype& prototype);

} // namespace heartstead::simulation
