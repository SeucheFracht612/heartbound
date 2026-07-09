#pragma once

#include "engine/items/item_stack.hpp"
#include "engine/modding/generic_prototype.hpp"

namespace heartstead::items {

[[nodiscard]] core::Result<ItemDefinition>
item_definition_from_prototype(const modding::GenericPrototype& prototype);

} // namespace heartstead::items
