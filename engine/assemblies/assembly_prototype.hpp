#pragma once

#include "engine/assemblies/assembly.hpp"
#include "engine/modding/generic_prototype.hpp"

namespace heartstead::assemblies {

[[nodiscard]] core::Result<AssemblyDefinition>
assembly_definition_from_prototype(const modding::GenericPrototype& prototype);

} // namespace heartstead::assemblies
