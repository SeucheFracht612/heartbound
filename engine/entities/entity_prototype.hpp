#pragma once

#include "engine/entities/entity.hpp"
#include "engine/modding/generic_prototype.hpp"

namespace heartstead::entities {

[[nodiscard]] core::Result<EntityDefinition>
entity_definition_from_prototype(const modding::GenericPrototype& prototype);

} // namespace heartstead::entities
