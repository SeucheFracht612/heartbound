#pragma once

#include "engine/core/ids.hpp"

namespace heartstead::entities {

struct EntityIdTag;
using EntityId = core::GenerationalHandle<EntityIdTag>;

} // namespace heartstead::entities
