#pragma once

#include "engine/entities/entity_world.hpp"
#include "engine/modding/prototype_registry.hpp"
#include "engine/net/host_session.hpp"
#include "engine/physics/physics_world.hpp"
#include "engine/simulation/simulation_scheduler.hpp"
#include "engine/world/replication_delta.hpp"
#include "engine/world/voxels/voxel_palette.hpp"
#include "engine/world/world_state.hpp"

#include <cstdint>
#include <memory>

namespace heartstead::game {

struct ServerRuntimeDesc {
    world::WorldStateDesc world;
    net::HostSessionConfig host;
    physics::PhysicsWorldDesc physics;
    const modding::PrototypeRegistry* prototypes = nullptr;
    const world::VoxelPalette* voxel_palette = nullptr;
};

struct ServerRuntimeTickStats {
    simulation::SimulationTickStats simulation;
    net::HostSessionTickResult commands;
    world::WorldReplicationDeltaDeliveryReport replication;
    physics::PhysicsStepStats physics;
};

class ServerRuntime final {
  public:
    [[nodiscard]] static core::Result<std::unique_ptr<ServerRuntime>> create(ServerRuntimeDesc desc);

    ServerRuntime(const ServerRuntime&) = delete;
    ServerRuntime& operator=(const ServerRuntime&) = delete;

    [[nodiscard]] core::Status start();
    [[nodiscard]] core::Status stop();
    [[nodiscard]] core::Result<ServerRuntimeTickStats>
    run_tick(std::uint64_t tick, double fixed_delta_seconds, std::int64_t now_ms);

    [[nodiscard]] core::Result<core::NetId> connect_client();
    [[nodiscard]] core::Status disconnect_client(core::NetId client_id);
    [[nodiscard]] core::Status submit_command(core::NetId client_id,
                                              net::CommandEnvelope command);
    [[nodiscard]] core::Result<std::vector<net::TransportEnvelope>>
    drain_client_messages(core::NetId client_id);

    [[nodiscard]] bool is_running() const noexcept;
    [[nodiscard]] world::WorldState& world() noexcept;
    [[nodiscard]] const world::WorldState& world() const noexcept;
    [[nodiscard]] entities::EntityWorld& entities() noexcept;
    [[nodiscard]] const entities::EntityWorld& entities() const noexcept;
    [[nodiscard]] net::HostSession& host() noexcept;
    [[nodiscard]] const simulation::SimulationScheduler& scheduler() const noexcept;
    [[nodiscard]] const simulation::TickEvents& events() const noexcept;

  private:
    explicit ServerRuntime(ServerRuntimeDesc desc);
    [[nodiscard]] core::Status initialize();

    ServerRuntimeDesc desc_;
    world::WorldState world_;
    entities::EntityWorld entities_;
    std::unique_ptr<physics::IPhysicsWorld> physics_;
    net::HostSession host_;
    net::ServerCommandDispatcher commands_;
    simulation::SimulationScheduler scheduler_;
    simulation::TickEvents events_;
    net::HostSessionTickResult current_commands_;
    world::WorldReplicationDeltaDeliveryReport current_replication_;
    physics::PhysicsStepStats current_physics_;
    std::int64_t current_time_ms_ = 0;
};

} // namespace heartstead::game
