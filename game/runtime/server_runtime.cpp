#include "game/runtime/server_runtime.hpp"

#include "engine/world/world_commands.hpp"

#include <utility>

namespace heartstead::game {

ServerRuntime::ServerRuntime(ServerRuntimeDesc desc)
    : desc_(std::move(desc)), world_(desc_.world), host_(desc_.host) {}

core::Result<std::unique_ptr<ServerRuntime>> ServerRuntime::create(ServerRuntimeDesc desc) {
    if (desc.prototypes == nullptr || desc.voxel_palette == nullptr) {
        return core::Result<std::unique_ptr<ServerRuntime>>::failure(
            "server_runtime.missing_content",
            "authoritative runtime requires immutable prototype and voxel registries");
    }
    auto runtime = std::unique_ptr<ServerRuntime>(new ServerRuntime(std::move(desc)));
    auto status = runtime->initialize();
    if (!status) {
        return core::Result<std::unique_ptr<ServerRuntime>>::failure(status.error().code,
                                                                     status.error().message);
    }
    return core::Result<std::unique_ptr<ServerRuntime>>::success(std::move(runtime));
}

core::Status ServerRuntime::initialize() {
    auto physics = physics::create_physics_world(desc_.physics);
    if (!physics) {
        return core::Status::failure(physics.error().code, physics.error().message);
    }
    physics_ = std::move(physics).value();

    auto status = world::WorldCommandRegistry::register_engine_commands(commands_);
    if (!status) {
        return status;
    }
    status = scheduler_.register_system({
        "runtime.command_gateway", simulation::SimulationPhase::commands, {},
        [this](simulation::SimulationContext&) {
            net::CommandExecutionContext command_context;
            command_context.executor_role = net::CommandExecutorRole::authoritative_server;
            command_context.server_time_ms = current_time_ms_;
            command_context.save_ids = &world_.save_ids();
            command_context.prototypes = desc_.prototypes;
            command_context.world_state = &world_;
            command_context.voxel_palette = desc_.voxel_palette;
            auto result = host_.tick(commands_, command_context);
            if (!result) {
                return core::Status::failure(result.error().code, result.error().message);
            }
            current_commands_ = std::move(result).value();
            return core::Status::ok();
        },
    });
    if (!status) {
        return status;
    }
    status = scheduler_.register_system({
        "runtime.physics", simulation::SimulationPhase::physics,
        {"runtime.command_gateway"},
        [this](simulation::SimulationContext& context) {
            auto result = physics_->step(
                physics::PhysicsStepDesc{static_cast<float>(context.fixed_delta_seconds)});
            if (!result) {
                return core::Status::failure(result.error().code, result.error().message);
            }
            current_physics_ = result.value();
            return core::Status::ok();
        },
    });
    if (!status) {
        return status;
    }
    status = scheduler_.register_system({
        "runtime.world_clock", simulation::SimulationPhase::environment,
        {"runtime.physics"},
        [this](simulation::SimulationContext&) { return world_.advance_world_time(1); },
    });
    if (!status) {
        return status;
    }
    status = scheduler_.register_system({
        "runtime.entity_finalize", simulation::SimulationPhase::finalize,
        {"runtime.world_clock"},
        [this](simulation::SimulationContext& context) {
            auto result = entities_.finalize_destruction(context.tick, context.events);
            return result ? core::Status::ok()
                          : core::Status::failure(result.error().code, result.error().message);
        },
    });
    if (!status) {
        return status;
    }
    status = scheduler_.register_system({
        "runtime.replication", simulation::SimulationPhase::replication,
        {"runtime.entity_finalize"},
        [this](simulation::SimulationContext&) {
            const auto deltas =
                world::materialize_replication_deltas_for_tick(world_, current_commands_);
            auto delivery = world::send_replication_delta_snapshots_for_tick(
                host_, deltas, current_commands_, current_time_ms_);
            if (!delivery) {
                return core::Status::failure(delivery.error().code, delivery.error().message);
            }
            current_replication_ = std::move(delivery).value();
            return core::Status::ok();
        },
    });
    if (!status) {
        return status;
    }
    return scheduler_.finalize();
}

core::Status ServerRuntime::start() {
    return host_.start();
}

core::Status ServerRuntime::stop() {
    if (!host_.is_running()) {
        return core::Status::ok();
    }
    return host_.stop();
}

core::Result<ServerRuntimeTickStats>
ServerRuntime::run_tick(std::uint64_t tick, double fixed_delta_seconds, std::int64_t now_ms) {
    if (!is_running()) {
        return core::Result<ServerRuntimeTickStats>::failure(
            "server_runtime.not_running", "server runtime must be started before ticking");
    }
    events_.clear();
    current_time_ms_ = now_ms;
    current_commands_ = {};
    current_replication_ = {};
    current_physics_ = {};
    auto simulation = scheduler_.run_tick(
        {tick, fixed_delta_seconds, &world_, physics_.get(), &events_});
    if (!simulation) {
        return core::Result<ServerRuntimeTickStats>::failure(simulation.error().code,
                                                             simulation.error().message);
    }
    return core::Result<ServerRuntimeTickStats>::success(
        {simulation.value(), current_commands_, current_replication_, current_physics_});
}

core::Result<core::NetId> ServerRuntime::connect_client() {
    return host_.connect_client();
}

core::Status ServerRuntime::disconnect_client(core::NetId client_id) {
    return host_.disconnect_client(client_id);
}

core::Status ServerRuntime::submit_command(core::NetId client_id, net::CommandEnvelope command) {
    return host_.send_client_command(client_id, std::move(command));
}

core::Result<std::vector<net::TransportEnvelope>>
ServerRuntime::drain_client_messages(core::NetId client_id) {
    return host_.drain_client_messages(client_id);
}

bool ServerRuntime::is_running() const noexcept {
    return host_.is_running();
}

world::WorldState& ServerRuntime::world() noexcept {
    return world_;
}

const world::WorldState& ServerRuntime::world() const noexcept {
    return world_;
}

entities::EntityWorld& ServerRuntime::entities() noexcept {
    return entities_;
}

const entities::EntityWorld& ServerRuntime::entities() const noexcept {
    return entities_;
}

net::HostSession& ServerRuntime::host() noexcept {
    return host_;
}

const simulation::SimulationScheduler& ServerRuntime::scheduler() const noexcept {
    return scheduler_;
}

const simulation::TickEvents& ServerRuntime::events() const noexcept {
    return events_;
}

} // namespace heartstead::game
