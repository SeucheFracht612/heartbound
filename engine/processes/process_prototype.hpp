#pragma once

#include "engine/modding/generic_prototype.hpp"
#include "engine/processes/process.hpp"

namespace heartstead::processes {

[[nodiscard]] core::Result<ProcessDefinition>
process_definition_from_prototype(const modding::GenericPrototype& prototype);

} // namespace heartstead::processes
