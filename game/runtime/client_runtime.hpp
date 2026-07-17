#pragma once

#include "engine/net/client_session.hpp"
#include "engine/movement/movement_prediction.hpp"
#include "engine/world/replication_delta.hpp"
#include "engine/world/world_state.hpp"

#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace heartstead::game {

struct ClientRuntimeStats {
    std::uint32_t received_message_count = 0;
    std::uint32_t command_result_count = 0;
    std::uint32_t movement_snapshot_count = 0;
    world::WorldClientReplicationApplyReport replication;
};

class ClientRuntime final {
  public:
    ClientRuntime(core::NetId expected_client_id, world::WorldStateDesc world_desc);

    [[nodiscard]] core::Status
    receive(std::span<const net::TransportEnvelope> messages);
    [[nodiscard]] core::Result<ClientRuntimeStats> synchronize();
    [[nodiscard]] core::Result<net::CommandEnvelope>
    create_command(std::string type, std::string payload, std::int64_t now_ms);

    [[nodiscard]] bool is_connected() const noexcept;
    [[nodiscard]] core::NetId client_id() const noexcept;
    [[nodiscard]] world::WorldState& world() noexcept;
    [[nodiscard]] const world::WorldState& world() const noexcept;
    [[nodiscard]] net::ClientSession& session() noexcept;
    [[nodiscard]] std::span<const net::HostSessionCommandResult> command_results() const noexcept;
    [[nodiscard]] const movement::PlayerControllerSnapshot*
    player_snapshot(core::NetId player_net_id) const noexcept;
    [[nodiscard]] std::vector<const movement::PlayerControllerSnapshot*>
    movement_snapshots() const;
    void clear_command_results() noexcept;

  private:
    world::WorldState world_;
    net::ClientSession session_;
    std::vector<net::HostSessionCommandResult> command_results_;
    std::unordered_map<std::uint64_t, movement::PlayerControllerSnapshot> movement_snapshots_;
    std::uint32_t messages_since_sync_ = 0;
};

} // namespace heartstead::game
