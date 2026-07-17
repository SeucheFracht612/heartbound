#include "engine/server/admin_service.hpp"

#include "engine/core/file_io.hpp"
#include "engine/core/filesystem.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <limits>
#include <ranges>
#include <set>
#include <utility>

namespace heartstead::server {

namespace {

constexpr std::size_t max_profile_import_bytes = 4U * 1024U * 1024U;
constexpr std::size_t max_ban_file_bytes = 16U * 1024U * 1024U;
constexpr std::size_t max_ban_records = 100'000;
constexpr std::size_t max_ban_address_hash_bytes = 256;
constexpr std::size_t max_ban_reason_bytes = 4U * 1024U;
constexpr std::size_t max_ban_actor_bytes = 256;
constexpr std::size_t max_ban_timestamp_bytes = 64;

[[nodiscard]] bool safe_text(std::string_view value) noexcept {
    return std::ranges::none_of(value, [](char character) {
        const auto byte = static_cast<unsigned char>(character);
        return character == '|' || byte < 0x20 || byte == 0x7f;
    });
}

[[nodiscard]] bool valid_ban_timestamp(std::string_view value) noexcept {
    if ((value.size() != 20 && value.size() != 24) || value[4] != '-' || value[7] != '-' ||
        value[10] != 'T' || value[13] != ':' || value[16] != ':' || value.back() != 'Z' ||
        (value.size() == 24 && value[19] != '.')) {
        return false;
    }
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (index == 4 || index == 7 || index == 10 || index == 13 || index == 16 ||
            index == value.size() - 1U || (value.size() == 24 && index == 19)) {
            continue;
        }
        if (value[index] < '0' || value[index] > '9') {
            return false;
        }
    }
    const auto decimal = [value](std::size_t offset, std::size_t count) noexcept {
        unsigned result = 0;
        for (std::size_t index = 0; index < count; ++index) {
            result = (result * 10U) + static_cast<unsigned>(value[offset + index] - '0');
        }
        return result;
    };
    const std::chrono::year_month_day date{std::chrono::year(static_cast<int>(decimal(0, 4))),
                                           std::chrono::month(decimal(5, 2)),
                                           std::chrono::day(decimal(8, 2))};
    return date.ok() && decimal(11, 2) <= 23U && decimal(14, 2) <= 59U && decimal(17, 2) <= 59U;
}

[[nodiscard]] core::Result<bool> regular_file_exists(const std::filesystem::path& path,
                                                     std::string_view label) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error == std::errc::no_such_file_or_directory ||
        (!error && status.type() == std::filesystem::file_type::not_found)) {
        return core::Result<bool>::success(false);
    }
    if (error) {
        return core::Result<bool>::failure("admin.path_check_failed", "could not inspect " +
                                                                          std::string(label) +
                                                                          ": " + error.message());
    }
    if (std::filesystem::is_symlink(status)) {
        return core::Result<bool>::failure("admin.unsafe_symlink",
                                           std::string(label) +
                                               " must not be a symbolic link: " + path.string());
    }
    if (!std::filesystem::is_regular_file(status)) {
        return core::Result<bool>::failure(
            "admin.invalid_path", std::string(label) + " must be a regular file: " + path.string());
    }
    return core::Result<bool>::success(true);
}

[[nodiscard]] core::Status ensure_directory(const std::filesystem::path& path,
                                            std::string_view label) {
    std::error_code error;
    std::filesystem::create_directories(path, error);
    if (error) {
        return core::Status::failure("admin.create_directory_failed", "could not create " +
                                                                          std::string(label) +
                                                                          ": " + error.message());
    }
    const auto status = std::filesystem::symlink_status(path, error);
    if (error) {
        return core::Status::failure("admin.path_check_failed", "could not inspect " +
                                                                    std::string(label) + ": " +
                                                                    error.message());
    }
    if (std::filesystem::is_symlink(status)) {
        return core::Status::failure("admin.unsafe_symlink",
                                     std::string(label) + " must not be a symbolic link");
    }
    if (!std::filesystem::is_directory(status)) {
        return core::Status::failure("admin.invalid_path",
                                     std::string(label) + " is not a directory");
    }
    return core::Status::ok();
}

[[nodiscard]] core::Status write_text_atomic(const std::filesystem::path& path,
                                             std::string_view text) {
    auto status = ensure_directory(path.parent_path(), "admin data directory");
    if (!status) {
        return status;
    }
    auto destination_exists = regular_file_exists(path, "admin data file");
    if (!destination_exists) {
        return core::Status::failure(destination_exists.error().code,
                                     destination_exists.error().message);
    }
    auto temporary = path;
    temporary += ".tmp";
    auto temporary_exists = regular_file_exists(temporary, "temporary admin data file");
    if (!temporary_exists) {
        return core::Status::failure(temporary_exists.error().code,
                                     temporary_exists.error().message);
    }
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            return core::Status::failure("admin.data_unwritable",
                                         "could not open temporary admin data file");
        }
        output.write(text.data(), static_cast<std::streamsize>(text.size()));
        output.close();
        if (!output) {
            std::error_code cleanup_error;
            std::filesystem::remove(temporary, cleanup_error);
            return core::Status::failure("admin.data_unwritable",
                                         "could not write temporary admin data file");
        }
    }
    const auto replace_error = core::replace_file(temporary, path);
    if (replace_error) {
        std::error_code cleanup_error;
        std::filesystem::remove(temporary, cleanup_error);
        return core::Status::failure("admin.data_replace_failed", replace_error.message());
    }
    return core::Status::ok();
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

[[nodiscard]] bool path_is_within(const std::filesystem::path& path,
                                  const std::filesystem::path& directory) {
    const auto relative = path.lexically_relative(directory);
    if (relative.empty() || relative.is_absolute()) {
        return false;
    }
    const auto first = relative.begin();
    return first != relative.end() && *first != "..";
}

[[nodiscard]] core::Result<std::filesystem::path> canonical_path(const std::filesystem::path& path,
                                                                 std::string_view label) {
    std::error_code error;
    auto canonical = std::filesystem::weakly_canonical(path, error);
    if (error) {
        return core::Result<std::filesystem::path>::failure(
            "admin.backup_path_failed",
            "could not resolve " + std::string(label) + " path: " + error.message());
    }
    return core::Result<std::filesystem::path>::success(std::move(canonical));
}

} // namespace

core::Status ServerBanRecord::validate() const {
    if ((player_uuid.has_value() && !player_uuid->is_valid()) ||
        (!player_uuid.has_value() && address_hash.empty())) {
        return core::Status::failure("admin.invalid_ban",
                                     "ban needs a player uuid or address hash");
    }
    if (reason.empty() || created_at_utc.empty() || created_by.empty() ||
        address_hash.size() > max_ban_address_hash_bytes || reason.size() > max_ban_reason_bytes ||
        created_by.size() > max_ban_actor_bytes ||
        created_at_utc.size() > max_ban_timestamp_bytes || !safe_text(reason) ||
        !safe_text(created_by) || !safe_text(created_at_utc) || !safe_text(address_hash)) {
        return core::Status::failure("admin.invalid_ban", "ban fields are incomplete or unsafe");
    }
    if (!valid_ban_timestamp(created_at_utc)) {
        return core::Status::failure("admin.invalid_ban",
                                     "ban creation time must be a valid UTC timestamp");
    }
    return core::Status::ok();
}

ServerAdminService::ServerAdminService(AdminServiceConfig config, AuthoritativeServer& server)
    : config_(std::move(config)), server_(server), profiles_(server.world_root()) {
    config_.world_root = server_.world_root();
    config_.server_root = server_.server_root();
    config_.server_session = server_.server_session();
    if (config_.backup_root.empty()) {
        config_.backup_root = config_.server_root / "backups";
        const auto world = config_.world_root.lexically_normal();
        const auto backup = config_.backup_root.lexically_normal();
        const auto relative = backup.lexically_relative(world);
        if (!relative.empty() && *relative.begin() != "..")
            config_.backup_root = config_.world_root.parent_path() /
                                  (config_.world_root.filename().string() + "_backups");
    }
    ban_validator_handle_ =
        server_.add_ban_validator([this](const auto& uuid, std::string_view address_hash) {
            return is_banned(uuid, address_hash);
        });
}

ServerAdminService::~ServerAdminService() {
    server_.remove_ban_validator(ban_validator_handle_);
}

std::filesystem::path ServerAdminService::ban_file() const {
    return config_.server_root / "bans.txt";
}

core::Status ServerAdminService::validate_configuration() const {
    if (config_.world_root.empty() || config_.server_root.empty() || config_.backup_root.empty() ||
        config_.server_session.empty() || config_.server_session.size() > 128U ||
        config_.world_root.lexically_normal() != server_.world_root().lexically_normal() ||
        config_.server_root.lexically_normal() != server_.server_root().lexically_normal() ||
        config_.server_session != server_.server_session() || ban_validator_handle_ == 0) {
        return core::Status::failure(
            "admin.invalid_config",
            "admin service paths and session must match its authoritative server");
    }
    return core::Status::ok();
}

core::Status ServerAdminService::require_initialized() const {
    if (!initialized_) {
        return core::Status::failure("admin.not_initialized",
                                     "admin service must load its ban list before use");
    }
    return core::Status::ok();
}

bool ServerAdminService::initialized() const noexcept {
    return initialized_;
}

core::Status ServerAdminService::load_bans() {
    auto status = validate_configuration();
    if (!status) {
        return status;
    }
    std::vector<ServerBanRecord> loaded;
    auto exists = regular_file_exists(ban_file(), "server ban list");
    if (!exists) {
        return core::Status::failure(exists.error().code, exists.error().message);
    }
    if (!exists.value()) {
        bans_.clear();
        initialized_ = true;
        return core::Status::ok();
    }
    auto text = core::read_text_file(ban_file(), {.maximum_bytes = max_ban_file_bytes});
    if (!text) {
        return core::Status::failure(text.error().code == "core.file_too_large"
                                         ? "admin.ban_file_too_large"
                                         : "admin.bans_unreadable",
                                     text.error().message);
    }

    std::set<player_profiles::PlayerUuid> seen_uuids;
    std::set<std::string> seen_addresses;
    std::size_t start = 0;
    while (start < text.value().size()) {
        const auto end = text.value().find('\n', start);
        auto line = end == std::string::npos
                        ? std::string_view(text.value()).substr(start)
                        : std::string_view(text.value()).substr(start, end - start);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        if (line.empty()) {
            return core::Status::failure("admin.invalid_ban_file",
                                         "ban list contains an empty record");
        }
        if (loaded.size() >= max_ban_records) {
            return core::Status::failure("admin.too_many_bans",
                                         "server ban list exceeds its record limit");
        }
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
        status = record.validate();
        if (!status) {
            return core::Status::failure("admin.invalid_ban_file", status.error().message);
        }
        if ((record.player_uuid && !seen_uuids.insert(*record.player_uuid).second) ||
            (!record.address_hash.empty() && !seen_addresses.insert(record.address_hash).second)) {
            return core::Status::failure("admin.duplicate_ban",
                                         "ban list contains a duplicate identity");
        }
        loaded.push_back(std::move(record));
        if (end == std::string::npos) {
            break;
        }
        start = end + 1U;
    }
    bans_ = std::move(loaded);
    initialized_ = true;
    return core::Status::ok();
}

core::Status ServerAdminService::save_bans() const {
    auto status = require_initialized();
    if (!status) {
        return status;
    }
    status = validate_configuration();
    if (!status) {
        return status;
    }
    if (bans_.size() > max_ban_records) {
        return core::Status::failure("admin.too_many_bans",
                                     "server ban list exceeds its record limit");
    }
    std::string encoded;
    std::set<player_profiles::PlayerUuid> seen_uuids;
    std::set<std::string> seen_addresses;
    for (const auto& record : bans_) {
        status = record.validate();
        if (!status) {
            return status;
        }
        if ((record.player_uuid && !seen_uuids.insert(*record.player_uuid).second) ||
            (!record.address_hash.empty() && !seen_addresses.insert(record.address_hash).second)) {
            return core::Status::failure("admin.duplicate_ban",
                                         "server ban list contains a duplicate identity");
        }
        std::string row = record.player_uuid ? record.player_uuid->value() : std::string{};
        row += '|';
        row += record.address_hash;
        row += '|';
        row += record.reason;
        row += '|';
        row += record.created_at_utc;
        row += '|';
        row += record.created_by;
        row += '\n';
        if (encoded.size() > max_ban_file_bytes ||
            row.size() > max_ban_file_bytes - encoded.size()) {
            return core::Status::failure("admin.ban_file_too_large",
                                         "server ban list exceeds its size limit");
        }
        encoded += row;
    }
    return write_text_atomic(ban_file(), encoded);
}

bool ServerAdminService::is_banned(const player_profiles::PlayerUuid& uuid,
                                   std::string_view address_hash) const noexcept {
    if (!initialized_) {
        return true;
    }
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
    auto status = require_initialized();
    if (!status) {
        return status;
    }
    const auto* player = server_.find(client_id);
    if (player == nullptr)
        return core::Status::failure("admin.player_not_connected", "player is not connected");
    const auto uuid = player->profile.player_uuid.value();
    status = audit("kick", actor, world_time, {{"player_uuid", uuid}, {"reason", reason}});
    if (!status)
        return status;
    return server_.leave(client_id, world_time);
}

core::Status ServerAdminService::ban(core::NetId client_id, std::string reason,
                                     simulation::WorldTick world_time, std::string actor) {
    auto status = require_initialized();
    if (!status) {
        return status;
    }
    const auto* player = server_.find(client_id);
    if (player == nullptr)
        return core::Status::failure("admin.player_not_connected", "player is not connected");
    ServerBanRecord record{player->profile.player_uuid, player->address_hash, std::move(reason),
                           server_logs::utc_now_iso8601(), actor};
    status = record.validate();
    if (!status)
        return status;
    if (is_banned(*record.player_uuid, record.address_hash)) {
        return core::Status::failure("admin.already_banned",
                                     "player UUID or address is already banned");
    }
    if (bans_.size() >= max_ban_records) {
        return core::Status::failure("admin.too_many_bans",
                                     "server ban list reached its record limit");
    }
    bans_.push_back(record);
    status = save_bans();
    if (!status) {
        bans_.pop_back();
        return status;
    }
    status = audit("ban", actor, world_time,
                   {{"address_hash", record.address_hash},
                    {"player_uuid", record.player_uuid->value()},
                    {"reason", record.reason}});
    if (!status) {
        bans_.pop_back();
        auto rollback = save_bans();
        if (!rollback) {
            bans_.push_back(record);
            return core::Status::failure("admin.ban_audit_rollback_failed",
                                         status.error().message +
                                             "; ban rollback failed: " + rollback.error().message);
        }
        return status;
    }
    return server_.leave(client_id, world_time);
}

core::Status ServerAdminService::unban(const player_profiles::PlayerUuid& uuid,
                                       simulation::WorldTick world_time, std::string actor) {
    auto status = require_initialized();
    if (!status) {
        return status;
    }
    const auto old_size = bans_.size();
    const auto old_bans = bans_;
    std::erase_if(bans_, [&uuid](const auto& record) { return record.player_uuid == uuid; });
    if (old_size == bans_.size())
        return core::Status::failure("admin.ban_not_found", "player is not banned");
    status = save_bans();
    if (!status) {
        bans_ = old_bans;
        return status;
    }
    status = audit("unban", std::move(actor), world_time, {{"player_uuid", uuid.value()}});
    if (!status) {
        const auto unbanned_state = bans_;
        bans_ = old_bans;
        auto rollback = save_bans();
        if (!rollback) {
            bans_ = unbanned_state;
            return core::Status::failure(
                "admin.unban_audit_rollback_failed",
                status.error().message + "; unban rollback failed: " + rollback.error().message);
        }
    }
    return status;
}

core::Status ServerAdminService::grant_role(const player_profiles::PlayerUuid& uuid,
                                            std::string role, simulation::WorldTick world_time,
                                            std::string actor) {
    auto status = require_initialized();
    if (!status) {
        return status;
    }
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
    status = server_.replace_profile(std::move(profile).value());
    if (!status)
        return status;
    return audit("permission_grant", std::move(actor), world_time,
                 {{"player_uuid", uuid.value()}, {"role", std::move(role)}});
}

core::Status ServerAdminService::revoke_role(const player_profiles::PlayerUuid& uuid,
                                             std::string_view role,
                                             simulation::WorldTick world_time, std::string actor) {
    auto status = require_initialized();
    if (!status) {
        return status;
    }
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
    status = server_.replace_profile(std::move(profile).value());
    if (!status)
        return status;
    return audit("permission_revoke", std::move(actor), world_time,
                 {{"player_uuid", uuid.value()}, {"role", std::string(role)}});
}

core::Status ServerAdminService::reset_map(const player_profiles::PlayerUuid& uuid,
                                           std::optional<std::string_view> layer,
                                           simulation::WorldTick world_time, std::string actor) {
    auto status = require_initialized();
    if (!status) {
        return status;
    }
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
    status = server_.replace_profile(std::move(profile).value());
    if (!status)
        return status;
    return audit("map_reset", std::move(actor), world_time,
                 {{"player_uuid", uuid.value()},
                  {"layer", layer ? std::string(*layer) : "all"},
                  {"removed_regions", std::to_string(removed_regions)}});
}

core::Status ServerAdminService::export_profile(const player_profiles::PlayerUuid& uuid,
                                                const std::filesystem::path& destination,
                                                simulation::WorldTick world_time,
                                                std::string actor) const {
    auto status = require_initialized();
    if (!status) {
        return status;
    }
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
    output.close();
    if (!output)
        return core::Status::failure("admin.profile_export_failed",
                                     "could not write profile export");
    return audit("profile_export", std::move(actor), world_time,
                 {{"player_uuid", uuid.value()}, {"destination", destination.generic_string()}});
}

core::Status ServerAdminService::import_profile(const std::filesystem::path& source,
                                                simulation::WorldTick world_time,
                                                std::string actor) {
    auto status = require_initialized();
    if (!status) {
        return status;
    }
    std::error_code error;
    const auto size = std::filesystem::file_size(source, error);
    if (error) {
        return core::Status::failure("admin.profile_import_stat_failed", error.message());
    }
    if (size > max_profile_import_bytes)
        return core::Status::failure("admin.profile_import_too_large",
                                     "profile import exceeds its size limit");
    std::ifstream input(source, std::ios::binary);
    if (!input)
        return core::Status::failure("admin.profile_import_failed",
                                     "could not open profile import");
    std::string text(static_cast<std::size_t>(size), '\0');
    if (!text.empty()) {
        input.read(text.data(), static_cast<std::streamsize>(text.size()));
    }
    if (input.bad() || static_cast<std::size_t>(input.gcount()) != text.size()) {
        return core::Status::failure("admin.profile_import_failed",
                                     "could not read complete profile import");
    }
    if (input.peek() != std::char_traits<char>::eof()) {
        return core::Status::failure("admin.profile_import_changed",
                                     "profile import changed while it was being read");
    }
    auto profile = player_profiles::PlayerProfileTextCodec::decode(text);
    if (!profile)
        return core::Status::failure(profile.error().code, profile.error().message);
    const auto uuid = profile.value().player_uuid;
    status = server_.replace_profile(std::move(profile).value());
    if (!status)
        return status;
    return audit("profile_import", std::move(actor), world_time,
                 {{"player_uuid", uuid.value()}, {"source", source.generic_string()}});
}

core::Result<std::filesystem::path>
ServerAdminService::create_backup(std::string label, simulation::WorldTick world_time,
                                  std::string actor) const {
    auto initialization_status = require_initialized();
    if (!initialization_status) {
        return core::Result<std::filesystem::path>::failure(initialization_status.error().code,
                                                            initialization_status.error().message);
    }
    if (!core::is_valid_local_id(label)) {
        return core::Result<std::filesystem::path>::failure("admin.invalid_backup_label",
                                                            "backup label must be a local id");
    }
    std::error_code error;
    std::filesystem::create_directories(config_.backup_root, error);
    if (error) {
        return core::Result<std::filesystem::path>::failure("admin.backup_failed", error.message());
    }
    if (!std::filesystem::is_directory(config_.world_root, error) || error) {
        return core::Result<std::filesystem::path>::failure(
            "admin.backup_world_missing", "backup source world is not a readable directory");
    }

    auto canonical_world = canonical_path(config_.world_root, "world");
    auto canonical_backup = canonical_path(config_.backup_root, "backup root");
    if (!canonical_world || !canonical_backup) {
        const auto& path_error =
            !canonical_world ? canonical_world.error() : canonical_backup.error();
        return core::Result<std::filesystem::path>::failure(path_error.code, path_error.message);
    }
    if (path_is_within(canonical_backup.value(), canonical_world.value())) {
        return core::Result<std::filesystem::path>::failure(
            "admin.backup_inside_world", "backup root must not be inside the world being copied");
    }

    auto timestamp = server_logs::utc_now_iso8601();
    std::ranges::replace(timestamp, ':', '-');
    std::filesystem::path destination;
    std::filesystem::path staging;
    for (std::uint32_t suffix = 0; suffix < 1'000; ++suffix) {
        auto name = timestamp + "_" + label;
        if (suffix != 0) {
            name += "_" + std::to_string(suffix);
        }
        destination = canonical_backup.value() / name;
        staging = destination;
        staging += ".tmp";
        const bool destination_exists = std::filesystem::exists(destination, error);
        if (error) {
            break;
        }
        const bool staging_exists = std::filesystem::exists(staging, error);
        if (error) {
            break;
        }
        if (!destination_exists && !staging_exists) {
            break;
        }
        destination.clear();
        staging.clear();
    }
    if (error || destination.empty()) {
        return core::Result<std::filesystem::path>::failure(
            "admin.backup_name_exhausted",
            error ? error.message() : "could not allocate a unique backup directory name");
    }

    std::filesystem::copy(canonical_world.value(), staging,
                          std::filesystem::copy_options::recursive |
                              std::filesystem::copy_options::copy_symlinks,
                          error);
    if (error) {
        std::error_code cleanup_error;
        std::filesystem::remove_all(staging, cleanup_error);
        return core::Result<std::filesystem::path>::failure("admin.backup_failed", error.message());
    }

    std::filesystem::rename(staging, destination, error);
    if (error) {
        std::error_code cleanup_error;
        std::filesystem::remove_all(staging, cleanup_error);
        return core::Result<std::filesystem::path>::failure("admin.backup_publish_failed",
                                                            error.message());
    }

    auto status = audit("backup_created", std::move(actor), world_time,
                        {{"path", destination.generic_string()}});
    if (!status) {
        std::error_code cleanup_error;
        std::filesystem::remove_all(destination, cleanup_error);
        if (cleanup_error) {
            return core::Result<std::filesystem::path>::failure(
                "admin.backup_audit_rollback_failed",
                status.error().message + "; backup cleanup failed: " + cleanup_error.message());
        }
        return core::Result<std::filesystem::path>::failure(status.error().code,
                                                            status.error().message);
    }
    return core::Result<std::filesystem::path>::success(destination);
}

} // namespace heartstead::server
