#include "engine/physics/jolt/jolt_backend.hpp"

namespace heartstead::physics::jolt {

PhysicsBackendInfo backend_info() noexcept {
    return PhysicsBackendInfo{
        PhysicsBackend::jolt,
        physics_backend_name(PhysicsBackend::jolt),
        false,
        "jolt backend is not compiled in yet",
    };
}

core::Result<std::unique_ptr<IPhysicsWorld>> create_world(PhysicsWorldDesc) {
    return core::Result<std::unique_ptr<IPhysicsWorld>>::failure(
        "physics.jolt_unavailable", "jolt backend is not compiled in yet");
}

} // namespace heartstead::physics::jolt
