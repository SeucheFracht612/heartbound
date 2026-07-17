#pragma once

#include "engine/net/host_session.hpp"
#include "engine/player_profiles/player_profile.hpp"
#include "engine/server_logs/server_log.hpp"
#include "engine/simulation/world_time.hpp"

#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <string_view>

namespace heartstead::server {

struct PlayerJoinRequest {
    player_profiles::PlayerUuid player_uuid;
    std::string display_name;
    std::string remote_address;
    std::string client_content_fingerprint;
};

using JoinCompatibilityValidator = std::function<core::Status(const PlayerJoinRequest&)>;
using JoinBanValidator = std::function<bool(const player_profiles::PlayerUuid&, std::string_view)>;

struct AuthoritativeServerConfig {
    std::filesystem::path world_root;
    std::filesystem::path server_root;
    std::string server_session = "session";
    std::string expected_content_fingerprint;
    net::HostSessionConfig host;
    simulation::WorldTimeConfig world_time;
    JoinCompatibilityValidator compatibility_validator;
    JoinBanValidator ban_validator;
};

struct ConnectedPlayer {
    core::NetId client_id;
    player_profiles::PlayerProfile profile;
    bool dirty = false;
};

class AuthoritativeServer {
  public:
    explicit AuthoritativeServer(AuthoritativeServerConfig config);

    [[nodiscard]] core::Status start();
    [[nodiscard]] core::Status stop(simulation::WorldTick world_time);
    [[nodiscard]] bool is_running() const noexcept;
    [[nodiscard]] std::size_t connected_player_count() const noexcept;

    [[nodiscard]] core::Result<core::NetId> join(const PlayerJoinRequest& request,
                                                 simulation::WorldTick world_time);
    [[nodiscard]] core::Status leave(core::NetId client_id, simulation::WorldTick world_time);
    [[nodiscard]] core::Result<bool> discover(core::NetId client_id, std::string_view layer_id,
                                              player_profiles::MapCellCoord cell,
                                              simulation::WorldTick world_time);
    [[nodiscard]] core::Status chat(core::NetId client_id, const std::string& channel,
                                    const std::string& message, simulation::WorldTick world_time);
    [[nodiscard]] core::Status flush_dirty_profiles();

    [[nodiscard]] core::Result<net::HostSessionTickResult>
    tick(const net::ServerCommandDispatcher& dispatcher, net::CommandExecutionContext context);

    [[nodiscard]] ConnectedPlayer* find(core::NetId client_id) noexcept;
    [[nodiscard]] const ConnectedPlayer* find(core::NetId client_id) const noexcept;
    [[nodiscard]] ConnectedPlayer* find(const player_profiles::PlayerUuid& uuid) noexcept;
    [[nodiscard]] const ConnectedPlayer*
    find(const player_profiles::PlayerUuid& uuid) const noexcept;
    [[nodiscard]] core::Status replace_profile(player_profiles::PlayerProfile profile,
                                               bool notify_connected = true);
    [[nodiscard]] net::HostSession& host_session() noexcept;
    [[nodiscard]] const player_profiles::FilePlayerProfileStore& profile_store() const noexcept;
    [[nodiscard]] server_logs::FileServerLog& logs() noexcept;
    void set_ban_validator(JoinBanValidator validator);

  private:
    [[nodiscard]] core::Status send_profile_snapshot(const ConnectedPlayer& player);
    [[nodiscard]] core::Status send_map_update(const ConnectedPlayer& player,
                                               std::string_view layer_id,
                                               player_profiles::MapDiscoveryRegionCoord region);
    [[nodiscard]] std::string calendar(simulation::WorldTick world_time) const;
    [[nodiscard]] core::Status audit_command(const net::HostSessionCommandReport& report,
                                             simulation::WorldTick world_time);

    AuthoritativeServerConfig config_;
    net::HostSession host_;
    player_profiles::FilePlayerProfileStore profiles_;
    server_logs::FileServerLog logs_;
    std::map<std::uint64_t, ConnectedPlayer> players_;
    std::uint64_t next_outbound_sequence_ = 1;
};

[[nodiscard]] std::string hash_client_address(std::string_view address);

} // namespace heartstead::server
