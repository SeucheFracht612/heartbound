#pragma once

#include "engine/cargo/cargo.hpp"
#include "engine/modding/generic_prototype.hpp"

namespace heartstead::cargo {

[[nodiscard]] core::Result<CargoDefinition>
cargo_definition_from_prototype(const modding::GenericPrototype& prototype);

} // namespace heartstead::cargo
