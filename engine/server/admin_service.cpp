#include "engine/server/admin_service.hpp"

#include "engine/core/filesystem.hpp"

#include <algorithm>
#include <fstream>
#include <limits>
#include <ranges>
#include <set>
#include <utility>

namespace heartstead::server {

namespace {

constexpr std::uintmax_t max_profile_import_bytes = 4U * 1024U * 1024U;

[[nodiscard]] bool safe_text(std::string_view value) noexcept {
    return value.find_first_of("\r\n|") == std::string_view::npos;
}

[[nodiscard]] std::vector<std::string_view> split(std::string_view value, char delimiter) {
    std::vector<std::string_view> result;
    std::size_t start = 0;
    while (start <= value.size()) {
        const auto end = value.find(delimiter, start);
        if (end == std::string_view::npos) {
            result.push_back(value.substr(start));
            break;
        }
        result.push_back(value.substr(start, end - start));
        start = end + 1;
    }
    return result;
}

} // namespace

core::Status ServerBanRecord::validate() const {
    if ((!player_uuid.has_value() || !player_uuid->is_valid()) && address_hash.empty()) {
        return core::Status::failure("admin.invalid_ban",
                                     "ban needs a player uuid or address hash");
    }
    if (reason.empty() || created_at_utc.empty() || created_by.empty() || !safe_text(reason) ||
        !safe_text(created_by) || !safe_text(address_hash)) {
        return core::Status::failure("admin.invalid_ban", "ban fields are incomplete or unsafe");
    }
    return core::Status::ok();
}

ServerAdminService::ServerAdminService(AdminServiceConfig config, AuthoritativeServer& server)
    : config_(std::move(config)), server_(server), profiles_(config_.world_root) {
    if (config_.backup_root.empty()) {
        config_.backup_root = config_.server_root / "backups";
        const auto world = config_.world_root.lexically_normal();
        const auto backup = config_.backup_root.lexically_normal();
        const auto relative = backup.lexically_relative(world);
        if (!relative.empty() && *relative.begin() != "..")
            config_.backup_root = config_.world_root.parent_path() /
                                  (config_.world_root.filename().string() + "_backups");
    }
    server_.set_ban_validator([this](const auto& uuid, std::string_view address_hash) {
        return is_banned(uuid, address_hash);
    });
}

ServerAdminService::~ServerAdminService() {
    server_.set_ban_validator({});
}

std::filesystem::path ServerAdminService::ban_file() const {
    return config_.server_root / "bans.txt";
}

core::Status ServerAdminService::load_bans() {
    std::vector<ServerBanRecord> loaded;
    std::ifstream input(ban_file());
    if (!input) {
        if (!std::filesystem::exists(ban_file()))
            return core::Status::ok();
        return core::Status::failure("admin.bans_unreadable", "could not read server ban list");
    }
    std::string line;
    while (std::getline(input, line)) {
        const auto fields = split(line, '|');
        if (fields.size() != 5) {
            return core::Status::failure("admin.invalid_ban_file", "ban list record is malformed");
        }
        ServerBanRecord record;
        if (!fields[0].empty()) {
            auto uuid = player_profiles::PlayerUuid::parse(fields[0]);
            if (!uuid)
                return core::Status::failure("admin.invalid_ban_file", "ban uuid is invalid");
            record.player_uuid = *uuid;
        }
        record.address_hash = fields[1];
        record.reason = fields[2];
        record.created_at_utc = fields[3];
        record.created_by = fields[4];
        auto status = record.validate();
        if (!status)
            return status;
        loaded.push_back(std::move(record));
    }
    bans_ = std::move(loaded);
    return core::Status::ok();
}

core::Status ServerAdminService::save_bans() const {
    std::error_code error;
    std::filesystem::create_directories(config_.server_root, error);
    if (error)
        return core::Status::failure("admin.ban_directory_failed", error.message());
    const auto temporary = ban_file().string() + ".tmp";
    std::ofstream output(temporary, std::ios::trunc);
    if (!output)
        return core::Status::failure("admin.bans_unwritable", "could not write ban list");
    for (const auto& record : bans_) {
        auto status = record.validate();
        if (!status)
            return status;
        output << (record.player_uuid ? record.player_uuid->value() : std::string{}) << '|'
               << record.address_hash << '|' << record.reason << '|' << record.created_at_utc << '|'
               << record.created_by << '\n';
    }
    output.close();
    if (!output)
        return core::Status::failure("admin.bans_unwritable", "could not flush ban list");
    error = core::replace_file(temporary, ban_file());
    if (error) {
        std::error_code cleanup_error;
        std::filesystem::remove(temporary, cleanup_error);
    }
    return error ? core::Status::failure("admin.bans_replace_failed", error.message())
                 : core::Status::ok();
}

bool ServerAdminService::is_banned(const player_profiles::PlayerUuid& uuid,
                                   std::string_view address_hash) const noexcept {
    return std::ranges::any_of(bans_, [&](const auto& record) {
        return (record.player_uuid && *record.player_uuid == uuid) ||
               (!address_hash.empty() && record.address_hash == address_hash);
    });
}

const std::vector<ServerBanRecord>& ServerAdminService::bans() const noexcept {
    return bans_;
}

core::Status ServerAdminService::audit(std::string event, std::string actor,
                                       simulation::WorldTick world_time,
                                       std::map<std::string, std::string> metadata) const {
    server_logs::ServerLogEntry entry;
    entry.real_timestamp_utc = server_logs::utc_now_iso8601();
    entry.world_time = world_time;
    entry.world_calendar = "world_time=" + std::to_string(world_time);
    entry.event_type = std::move(event);
    entry.server_session = config_.server_session;
    entry.display_name = std::move(actor);
    entry.metadata = std::move(metadata);
    return server_.logs().append(server_logs::ServerLogCategory::audit, std::move(entry));
}

core::Status ServerAdminService::kick(core::NetId client_id, std::string reason,
                                      simulation::WorldTick world_time, std::string actor) {
    const auto* player = server_.find(client_id);
    if (player == nullptr)
        return core::Status::failure("admin.player_not_connected", "player is not connected");
    const auto uuid = player->profile.player_uuid.value();
    auto status = audit("KICK", actor, world_time, {{"player_uuid", uuid}, {"reason", reason}});
    if (!status)
        return status;
    return server_.leave(client_id, world_time);
}

core::Status ServerAdminService::ban(core::NetId client_id, std::string reason,
                                     simulation::WorldTick world_time, std::string actor) {
    const auto* player = server_.find(client_id);
    if (player == nullptr)
        return core::Status::failure("admin.player_not_connected", "player is not connected");
    ServerBanRecord record{
        player->profile.player_uuid, {}, std::move(reason), server_logs::utc_now_iso8601(), actor};
    auto status = record.validate();
    if (!status)
        return status;
    bans_.push_back(record);
    status = save_bans();
    if (!status) {
        bans_.pop_back();
        return status;
    }
    status = audit("BAN", actor, world_time,
                   {{"player_uuid", record.player_uuid->value()}, {"reason", record.reason}});
    if (!status)
        return status;
    return server_.leave(client_id, world_time);
}

core::Status ServerAdminService::unban(const player_profiles::PlayerUuid& uuid,
                                       simulation::WorldTick world_time, std::string actor) {
    const auto old_size = bans_.size();
    const auto old_bans = bans_;
    std::erase_if(bans_, [&uuid](const auto& record) { return record.player_uuid == uuid; });
    if (old_size == bans_.size())
        return core::Status::failure("admin.ban_not_found", "player is not banned");
    auto status = save_bans();
    if (!status) {
        bans_ = old_bans;
        return status;
    }
    return audit("UNBAN", std::move(actor), world_time, {{"player_uuid", uuid.value()}});
}

core::Status ServerAdminService::grant_role(const player_profiles::PlayerUuid& uuid,
                                            std::string role, simulation::WorldTick world_time,
                                            std::string actor) {
    if (!core::is_valid_local_id(role))
        return core::Status::failure("admin.invalid_role", "role must be a local id");
    auto profile =
        server_.find(uuid) != nullptr
            ? core::Result<player_profiles::PlayerProfile>::success(server_.find(uuid)->profile)
            : profiles_.load(uuid);
    if (!profile)
        return core::Status::failure(profile.error().code, profile.error().message);
    if (std::ranges::contains(profile.value().roles, role))
        return core::Status::failure("admin.role_already_granted", "player already has role");
    if (profile.value().revision == std::numeric_limits<std::uint64_t>::max())
        return core::Status::failure("admin.profile_revision_overflow",
                                     "player profile revision cannot overflow");
    profile.value().roles.push_back(role);
    ++profile.value().revision;
    auto status = server_.replace_profile(std::move(profile).value());
    if (!status)
        return status;
    return audit("PERMISSION_GRANT", std::move(actor), world_time,
                 {{"player_uuid", uuid.value()}, {"role", std::move(role)}});
}

core::Status ServerAdminService::revoke_role(const player_profiles::PlayerUuid& uuid,
                                             std::string_view role,
                                             simulation::WorldTick world_time, std::string actor) {
    auto profile =
        server_.find(uuid) != nullptr
            ? core::Result<player_profiles::PlayerProfile>::success(server_.find(uuid)->profile)
            : profiles_.load(uuid);
    if (!profile)
        return core::Status::failure(profile.error().code, profile.error().message);
    const auto removed = std::erase(profile.value().roles, role);
    if (removed == 0)
        return core::Status::failure("admin.role_not_found", "player does not have role");
    if (profile.value().revision == std::numeric_limits<std::uint64_t>::max())
        return core::Status::failure("admin.profile_revision_overflow",
                                     "player profile revision cannot overflow");
    ++profile.value().revision;
    auto status = server_.replace_profile(std::move(profile).value());
    if (!status)
        return status;
    return audit("PERMISSION_REVOKE", std::move(actor), world_time,
                 {{"player_uuid", uuid.value()}, {"role", std::string(role)}});
}

core::Status ServerAdminService::reset_map(const player_profiles::PlayerUuid& uuid,
                                           std::optional<std::string_view> layer,
                                           simulation::WorldTick world_time, std::string actor) {
    auto profile =
        server_.find(uuid) != nullptr
            ? core::Result<player_profiles::PlayerProfile>::success(server_.find(uuid)->profile)
            : profiles_.load(uuid);
    if (!profile)
        return core::Status::failure(profile.error().code, profile.error().message);
    if (layer && !core::is_valid_local_id(*layer))
        return core::Status::failure("admin.invalid_map_layer", "map layer id is invalid");
    std::size_t removed_regions = 0;
    if (layer)
        removed_regions = profile.value().map_discovery.clear_layer(*layer);
    else {
        removed_regions = profile.value().map_discovery.region_count();
        profile.value().map_discovery.clear();
    }
    if (removed_regions == 0)
        return core::Status::failure("admin.map_already_empty", "map selection is already empty");
    if (profile.value().revision == std::numeric_limits<std::uint64_t>::max())
        return core::Status::failure("admin.profile_revision_overflow",
                                     "player profile revision cannot overflow");
    ++profile.value().revision;
    auto status = server_.replace_profile(std::move(profile).value());
    if (!status)
        return status;
    return audit("MAP_RESET", std::move(actor), world_time,
                 {{"player_uuid", uuid.value()},
                  {"layer", layer ? std::string(*layer) : "all"},
                  {"removed_regions", std::to_string(removed_regions)}});
}

core::Status ServerAdminService::export_profile(const player_profiles::PlayerUuid& uuid,
                                                const std::filesystem::path& destination,
                                                simulation::WorldTick world_time,
                                                std::string actor) const {
    auto profile =
        server_.find(uuid) != nullptr
            ? core::Result<player_profiles::PlayerProfile>::success(server_.find(uuid)->profile)
            : profiles_.load(uuid);
    if (!profile)
        return core::Status::failure(profile.error().code, profile.error().message);
    std::ofstream output(destination, std::ios::trunc);
    if (!output)
        return core::Status::failure("admin.profile_export_failed", "could not open export path");
    output << player_profiles::PlayerProfileTextCodec::encode(profile.value());
    if (!output)
        return core::Status::failure("admin.profile_export_failed",
                                     "could not write profile export");
    return audit("PROFILE_EXPORT", std::move(actor), world_time,
                 {{"player_uuid", uuid.value()}, {"destination", destination.generic_string()}});
}

core::Status ServerAdminService::import_profile(const std::filesystem::path& source,
                                                simulation::WorldTick world_time,
                                                std::string actor) {
    std::error_code error;
    const auto size = std::filesystem::file_size(source, error);
    if (error || size > max_profile_import_bytes)
        return core::Status::failure("admin.profile_import_too_large",
                                     "profile import exceeds its size limit");
    std::ifstream input(source);
    if (!input)
        return core::Status::failure("admin.profile_import_failed",
                                     "could not open profile import");
    std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    auto profile = player_profiles::PlayerProfileTextCodec::decode(text);
    if (!profile)
        return core::Status::failure(profile.error().code, profile.error().message);
    const auto uuid = profile.value().player_uuid;
    auto status = server_.replace_profile(std::move(profile).value());
    if (!status)
        return status;
    return audit("PROFILE_IMPORT", std::move(actor), world_time,
                 {{"player_uuid", uuid.value()}, {"source", source.generic_string()}});
}

core::Result<std::filesystem::path>
ServerAdminService::create_backup(std::string label, simulation::WorldTick world_time,
                                  std::string actor) const {
    if (!core::is_valid_local_id(label)) {
        return core::Result<std::filesystem::path>::failure("admin.invalid_backup_label",
                                                            "backup label must be a local id");
    }
    auto timestamp = server_logs::utc_now_iso8601();
    std::ranges::replace(timestamp, ':', '-');
    const auto destination = config_.backup_root / (timestamp + "_" + label);
    const auto relative = config_.backup_root.lexically_normal().lexically_relative(
        config_.world_root.lexically_normal());
    if (!relative.empty() && *relative.begin() != "..") {
        return core::Result<std::filesystem::path>::failure(
            "admin.backup_inside_world", "backup root must not be inside the world being copied");
    }
    std::error_code error;
    std::filesystem::create_directories(config_.backup_root, error);
    if (!error) {
        std::filesystem::copy(config_.world_root, destination,
                              std::filesystem::copy_options::recursive |
                                  std::filesystem::copy_options::copy_symlinks,
                              error);
    }
    if (error)
        return core::Result<std::filesystem::path>::failure("admin.backup_failed", error.message());
    auto status = audit("BACKUP_CREATED", std::move(actor), world_time,
                        {{"path", destination.generic_string()}});
    if (!status)
        return core::Result<std::filesystem::path>::failure(status.error().code,
                                                            status.error().message);
    return core::Result<std::filesystem::path>::success(destination);
}

} // namespace heartstead::server
