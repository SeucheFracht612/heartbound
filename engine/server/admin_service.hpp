#pragma once

#include "engine/player_profiles/player_profile.hpp"
#include "engine/server/authoritative_server.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace heartstead::server {

struct ServerBanRecord {
    std::optional<player_profiles::PlayerUuid> player_uuid;
    std::string address_hash;
    std::string reason;
    std::string created_at_utc;
    std::string created_by;

    [[nodiscard]] core::Status validate() const;
};

struct AdminServiceConfig {
    std::filesystem::path world_root;
    std::filesystem::path server_root;
    std::filesystem::path backup_root;
    std::string server_session = "session";
};

class ServerAdminService {
  public:
    ServerAdminService(AdminServiceConfig config, AuthoritativeServer& server);
    ~ServerAdminService();

    [[nodiscard]] core::Status load_bans();
    [[nodiscard]] core::Status save_bans() const;
    [[nodiscard]] bool is_banned(const player_profiles::PlayerUuid& uuid,
                                 std::string_view address_hash) const noexcept;
    [[nodiscard]] const std::vector<ServerBanRecord>& bans() const noexcept;

    [[nodiscard]] core::Status kick(core::NetId client_id, std::string reason,
                                    simulation::WorldTick world_time, std::string actor);
    [[nodiscard]] core::Status ban(core::NetId client_id, std::string reason,
                                   simulation::WorldTick world_time, std::string actor);
    [[nodiscard]] core::Status unban(const player_profiles::PlayerUuid& uuid,
                                     simulation::WorldTick world_time, std::string actor);
    [[nodiscard]] core::Status grant_role(const player_profiles::PlayerUuid& uuid, std::string role,
                                          simulation::WorldTick world_time, std::string actor);
    [[nodiscard]] core::Status revoke_role(const player_profiles::PlayerUuid& uuid,
                                           std::string_view role, simulation::WorldTick world_time,
                                           std::string actor);
    [[nodiscard]] core::Status reset_map(const player_profiles::PlayerUuid& uuid,
                                         std::optional<std::string_view> layer,
                                         simulation::WorldTick world_time, std::string actor);
    [[nodiscard]] core::Status export_profile(const player_profiles::PlayerUuid& uuid,
                                              const std::filesystem::path& destination,
                                              simulation::WorldTick world_time,
                                              std::string actor) const;
    [[nodiscard]] core::Status import_profile(const std::filesystem::path& source,
                                              simulation::WorldTick world_time, std::string actor);
    [[nodiscard]] core::Result<std::filesystem::path>
    create_backup(std::string label, simulation::WorldTick world_time, std::string actor) const;

  private:
    [[nodiscard]] core::Status audit(std::string event, std::string actor,
                                     simulation::WorldTick world_time,
                                     std::map<std::string, std::string> metadata = {}) const;
    [[nodiscard]] std::filesystem::path ban_file() const;

    AdminServiceConfig config_;
    AuthoritativeServer& server_;
    player_profiles::FilePlayerProfileStore profiles_;
    std::vector<ServerBanRecord> bans_;
};

} // namespace heartstead::server
