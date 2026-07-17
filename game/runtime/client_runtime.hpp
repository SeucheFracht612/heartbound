#pragma once

#include "engine/net/client_session.hpp"
#include "engine/movement/movement_prediction.hpp"
#include "engine/world/chunks/chunk_replication.hpp"
#include "engine/world/voxels/voxel_palette.hpp"
#include "engine/world/replication_delta.hpp"
#include "engine/world/world_state.hpp"
#include "game/framework/gameplay_module.hpp"

#include <cstdint>
#include <array>
#include <map>
#include <span>
#include <unordered_map>
#include <vector>

namespace heartstead::game {

struct ClientRuntimeStats {
    std::uint32_t received_message_count = 0;
    std::uint32_t command_result_count = 0;
    std::uint32_t movement_snapshot_count = 0;
    std::uint32_t player_tombstone_count = 0;
    std::uint32_t chunk_snapshot_slice_count = 0;
    std::uint32_t completed_chunk_snapshot_count = 0;
    world::WorldClientReplicationApplyReport replication;
    ClientReplicationDispatchStats feature_replication;
};

class ClientRuntime final {
  public:
    ClientRuntime(core::NetId expected_client_id, world::WorldStateDesc world_desc,
                  const world::VoxelPalette* voxel_palette,
                  const ReplicationRegistry* replication_registry = nullptr);

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
    [[nodiscard]] core::NetId local_player_net_id() const noexcept;
    [[nodiscard]] const movement::PlayerControllerSnapshot* local_player_snapshot() const noexcept;
    [[nodiscard]] std::vector<const movement::PlayerControllerSnapshot*>
    movement_snapshots() const;
    [[nodiscard]] std::span<const core::NetId> player_tombstones() const noexcept;
    void clear_command_results() noexcept;

  private:
    struct ChunkSnapshotApplyStats {
        std::uint32_t slice_count = 0;
        std::uint32_t completed_chunk_count = 0;
    };

    struct ChunkSnapshotAssembly {
        world::ChunkIdentity identity;
        std::uint64_t content_revision = 0;
        std::array<std::vector<world::VoxelCell>, world::VoxelChunk::edge_length> slices;
        std::array<bool, world::VoxelChunk::edge_length> received{};
    };

    [[nodiscard]] core::Result<ChunkSnapshotApplyStats> apply_queued_chunk_snapshots();

    world::WorldState world_;
    const world::VoxelPalette* voxel_palette_ = nullptr;
    const ReplicationRegistry* replication_registry_ = nullptr;
    net::ClientSession session_;
    std::vector<net::HostSessionCommandResult> command_results_;
    std::unordered_map<std::uint64_t, movement::PlayerControllerSnapshot> movement_snapshots_;
    std::vector<core::NetId> player_tombstones_;
    core::NetId local_player_net_id_;
    std::map<world::ChunkCoord, ChunkSnapshotAssembly> chunk_snapshot_assemblies_;
    std::map<world::ChunkCoord, std::pair<world::ChunkIdentity, std::uint64_t>> remote_chunks_;
    std::uint32_t messages_since_sync_ = 0;
};

} // namespace heartstead::game
