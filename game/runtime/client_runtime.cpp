#include "game/runtime/client_runtime.hpp"

#include <algorithm>
#include <utility>

namespace heartstead::game {

ClientRuntime::ClientRuntime(core::NetId expected_client_id, world::WorldStateDesc world_desc)
    : world_(std::move(world_desc)), session_(expected_client_id) {}

core::Status ClientRuntime::receive(std::span<const net::TransportEnvelope> messages) {
    for (const auto& message : messages) {
        auto status = session_.receive_server_message(message);
        if (!status) {
            return status;
        }
        ++messages_since_sync_;
    }
    return core::Status::ok();
}

core::Result<ClientRuntimeStats> ClientRuntime::synchronize() {
    auto replication = world::apply_client_queued_replication_deltas(world_, session_);
    if (!replication) {
        return core::Result<ClientRuntimeStats>::failure(replication.error().code,
                                                         replication.error().message);
    }
    auto results = session_.drain_command_results();
    const auto result_count = static_cast<std::uint32_t>(results.size());
    command_results_.insert(command_results_.end(), std::make_move_iterator(results.begin()),
                            std::make_move_iterator(results.end()));
    auto movement_messages =
        session_.drain_replication_messages(movement::movement_snapshot_payload_type);
    std::uint32_t movement_snapshot_count = 0;
    for (const auto& message : movement_messages) {
        auto snapshot = movement::movement_snapshot_from_transport(message);
        if (!snapshot) {
            return core::Result<ClientRuntimeStats>::failure(snapshot.error().code,
                                                             snapshot.error().message);
        }
        const auto player_key = snapshot.value().player_net_id.value();
        const auto found = movement_snapshots_.find(player_key);
        if (found != movement_snapshots_.end() &&
            found->second.state.simulation_tick >= snapshot.value().state.simulation_tick) {
            continue;
        }
        movement_snapshots_.insert_or_assign(player_key, std::move(snapshot).value());
        ++movement_snapshot_count;
    }
    ClientRuntimeStats stats;
    stats.received_message_count = messages_since_sync_;
    stats.command_result_count = result_count;
    stats.movement_snapshot_count = movement_snapshot_count;
    stats.replication = std::move(replication).value();
    messages_since_sync_ = 0;
    return core::Result<ClientRuntimeStats>::success(std::move(stats));
}

core::Result<net::CommandEnvelope> ClientRuntime::create_command(std::string type,
                                                                 std::string payload,
                                                                 std::int64_t now_ms) {
    return session_.create_command(std::move(type), std::move(payload), now_ms);
}

bool ClientRuntime::is_connected() const noexcept {
    return session_.is_connected();
}

core::NetId ClientRuntime::client_id() const noexcept {
    return session_.client_id();
}

world::WorldState& ClientRuntime::world() noexcept {
    return world_;
}

const world::WorldState& ClientRuntime::world() const noexcept {
    return world_;
}

net::ClientSession& ClientRuntime::session() noexcept {
    return session_;
}

std::span<const net::HostSessionCommandResult> ClientRuntime::command_results() const noexcept {
    return command_results_;
}

const movement::PlayerControllerSnapshot*
ClientRuntime::player_snapshot(core::NetId player_net_id) const noexcept {
    const auto found = movement_snapshots_.find(player_net_id.value());
    return found == movement_snapshots_.end() ? nullptr : &found->second;
}

std::vector<const movement::PlayerControllerSnapshot*> ClientRuntime::movement_snapshots() const {
    std::vector<const movement::PlayerControllerSnapshot*> result;
    result.reserve(movement_snapshots_.size());
    for (const auto& [_, snapshot] : movement_snapshots_) {
        result.push_back(&snapshot);
    }
    std::ranges::sort(result, [](const auto* lhs, const auto* rhs) {
        return lhs->player_net_id.value() < rhs->player_net_id.value();
    });
    return result;
}

void ClientRuntime::clear_command_results() noexcept {
    command_results_.clear();
}

} // namespace heartstead::game
