#pragma once

#include "engine/physics/physics_world.hpp"

namespace heartstead::physics::jolt {

[[nodiscard]] PhysicsBackendInfo backend_info() noexcept;

[[nodiscard]] core::Result<std::unique_ptr<IPhysicsWorld>> create_world(PhysicsWorldDesc desc);

} // namespace heartstead::physics::jolt
