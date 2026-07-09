#pragma once

#include "engine/core/result.hpp"
#include "engine/modding/generic_prototype.hpp"
#include "engine/modding/prototype_registry.hpp"
#include "engine/renderer/materials/material_definition.hpp"

namespace heartstead::renderer::materials {

[[nodiscard]] core::Result<MaterialDefinition>
material_definition_from_prototype(const modding::GenericPrototype& prototype);

[[nodiscard]] core::Result<MaterialRegistry>
material_registry_from_prototypes(const modding::PrototypeRegistry& prototypes);

} // namespace heartstead::renderer::materials
