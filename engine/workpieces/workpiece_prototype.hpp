#pragma once

#include "engine/modding/generic_prototype.hpp"
#include "engine/workpieces/workpiece_definition.hpp"

namespace heartstead::workpieces {

[[nodiscard]] core::Result<WorkpieceDefinition>
workpiece_definition_from_prototype(const modding::GenericPrototype& prototype);

} // namespace heartstead::workpieces
