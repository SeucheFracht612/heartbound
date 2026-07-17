#include "engine/server/authoritative_server.hpp"

#include "engine/core/hash.hpp"
#include "engine/world/world_state.hpp"

#include <algorithm>
#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>

namespace heartstead::server {

namespace {

[[nodiscard]] net::TransportMessage profile_message(std::string type, std::string payload,
                                                    std::uint64_t sequence) {
    net::TransportMessage message;
    message.kind = net::TransportMessageKind::replication;
    message.channel = net::TransportChannel::reliable;
    message.sequence = sequence;
    message.payload_type = std::move(type);
    message.payload = std::move(payload);
    return message;
}

} // namespace

AuthoritativeServer::AuthoritativeServer(AuthoritativeServerConfig config)
    : config_(std::move(config)), host_(config_.host), profiles_(config_.world_root),
      logs_(config_.server_root.empty() ? config_.world_root / "server" : config_.server_root) {}

core::Status AuthoritativeServer::start() {
    if (config_.world_root.empty() || config_.server_session.empty() ||
        config_.server_session.size() > 128U) {
        return core::Status::failure("authoritative_server.invalid_config",
                                     "server requires a world root and a 1..128 byte session id");
    }
    auto status = config_.world_time.validate();
    if (!status)
        return status;
    status = logs_.initialize();
    if (!status)
        return status;
    return host_.start();
}

core::Status AuthoritativeServer::stop(simulation::WorldTick world_time) {
    std::vector<core::NetId> clients;
    clients.reserve(players_.size());
    for (const auto& [_, player] : players_)
        clients.push_back(player.client_id);
    for (const auto client : clients) {
        auto status = leave(client, world_time);
        if (!status)
            return status;
    }
    return host_.stop();
}

bool AuthoritativeServer::is_running() const noexcept {
    return host_.is_running();
}
std::size_t AuthoritativeServer::connected_player_count() const noexcept {
    return players_.size();
}

core::Result<core::NetId> AuthoritativeServer::join(const PlayerJoinRequest& request,
                                                    simulation::WorldTick world_time) {
    if (!is_running())
        return core::Result<core::NetId>::failure("authoritative_server.not_running",
                                                  "server is not running");
    if (!request.player_uuid.is_valid() || request.display_name.empty() ||
        request.display_name.size() > 128 || request.remote_address.size() > 1024 ||
        request.client_content_fingerprint.size() > 1024)
        return core::Result<core::NetId>::failure("authoritative_server.invalid_join",
                                                  "join requires UUID and display name");
    const auto address_hash = hash_client_address(request.remote_address);
    const auto is_banned =
        (config_.ban_validator && config_.ban_validator(request.player_uuid, address_hash)) ||
        std::ranges::any_of(ban_validators_, [&](const auto& validator) {
            return validator.second(request.player_uuid, address_hash);
        });
    if (is_banned) {
        return core::Result<core::NetId>::failure("authoritative_server.player_banned",
                                                  "player or address is banned from this server");
    }
    if (std::ranges::any_of(players_, [&request](const auto& entry) {
            return entry.second.profile.player_uuid == request.player_uuid;
        }))
        return core::Result<core::NetId>::failure("authoritative_server.already_connected",
                                                  "player UUID is already connected");
    if (!config_.expected_content_fingerprint.empty() &&
        request.client_content_fingerprint != config_.expected_content_fingerprint) {
        server_logs::ServerLogEntry mismatch{
            server_logs::utc_now_iso8601(),
            world_time,
            calendar(world_time),
            "mod_mismatch_join",
            config_.server_session,
            request.player_uuid,
            request.display_name,
            {},
            {},
            {{"client_fingerprint", request.client_content_fingerprint}}};
        auto log_status = logs_.append(server_logs::ServerLogCategory::audit, std::move(mismatch));
        if (!log_status) {
            return core::Result<core::NetId>::failure(log_status.error().code,
                                                      log_status.error().message);
        }
        return core::Result<core::NetId>::failure(
            "authoritative_server.content_mismatch",
            "client content fingerprint does not match server");
    }
    if (config_.compatibility_validator) {
        auto status = config_.compatibility_validator(request);
        if (!status)
            return core::Result<core::NetId>::failure(status.error().code, status.error().message);
    }
    auto profile = profiles_.load_or_create(request.player_uuid, request.display_name);
    if (!profile)
        return core::Result<core::NetId>::failure(profile.error().code, profile.error().message);
    auto name_status = profile.value().remember_display_name(request.display_name);
    if (!name_status)
        return core::Result<core::NetId>::failure(name_status.error().code,
                                                  name_status.error().message);
    auto client = host_.connect_client();
    if (!client)
        return client;
    ConnectedPlayer connected{client.value(), std::move(profile).value(), address_hash, true};
    auto [it, inserted] = players_.emplace(client.value().value(), std::move(connected));
    if (!inserted) {
        (void)host_.disconnect_client(client.value());
        return core::Result<core::NetId>::failure("authoritative_server.client_collision",
                                                  "transport client id collision");
    }
    auto status = host_.send_replication_message(
        client.value(),
        profile_message("world_config",
                        "ticks_per_second=" + std::to_string(config_.world_time.ticks_per_second) +
                            "|hours_per_day=" + std::to_string(config_.world_time.hours_per_day) +
                            "|content=" + config_.expected_content_fingerprint,
                        next_outbound_sequence_++));
    if (status) {
        status = host_.send_replication_message(
            client.value(),
            profile_message("world_time", std::to_string(world_time), next_outbound_sequence_++));
    }
    if (status)
        status = send_profile_snapshot(it->second);
    if (!status) {
        players_.erase(it);
        (void)host_.disconnect_client(client.value());
        return core::Result<core::NetId>::failure(status.error().code, status.error().message);
    }
    status = logs_.append_join(request.player_uuid, request.display_name, address_hash, world_time,
                               calendar(world_time), config_.server_session);
    if (!status) {
        players_.erase(client.value().value());
        (void)host_.disconnect_client(client.value());
        return core::Result<core::NetId>::failure(status.error().code, status.error().message);
    }
    return client;
}

core::Status AuthoritativeServer::leave(core::NetId client_id, simulation::WorldTick world_time) {
    auto found = players_.find(client_id.value());
    if (found == players_.end())
        return core::Status::failure("authoritative_server.unknown_client",
                                     "client is not connected");
    auto status = profiles_.save(found->second.profile);
    if (!status)
        return status;
    status = logs_.append_leave(found->second.profile.player_uuid,
                                std::string(found->second.profile.current_display_name()),
                                world_time, calendar(world_time), config_.server_session);
    if (!status)
        return status;
    status = host_.disconnect_client(client_id);
    if (!status)
        return status;
    players_.erase(found);
    return core::Status::ok();
}

core::Result<bool> AuthoritativeServer::discover(core::NetId client_id, std::string_view layer_id,
                                                 player_profiles::MapCellCoord cell,
                                                 simulation::WorldTick world_time) {
    auto* player = find(client_id);
    if (player == nullptr)
        return core::Result<bool>::failure("authoritative_server.unknown_client",
                                           "client is not connected");
    if (player->profile.revision == std::numeric_limits<std::uint64_t>::max())
        return core::Result<bool>::failure("authoritative_server.profile_revision_overflow",
                                           "player profile revision cannot overflow");
    auto changed = player->profile.map_discovery.discover(layer_id, cell);
    if (!changed || !changed.value())
        return changed;
    ++player->profile.revision;
    player->dirty = true;
    auto status =
        send_map_update(*player, layer_id, player_profiles::map_discovery_region_coord(cell));
    if (!status)
        return core::Result<bool>::failure(status.error().code, status.error().message);
    (void)world_time;
    return changed;
}

core::Status AuthoritativeServer::chat(core::NetId client_id, const std::string& channel,
                                       const std::string& message,
                                       simulation::WorldTick world_time) {
    auto* player = find(client_id);
    if (player == nullptr)
        return core::Status::failure("authoritative_server.unknown_client",
                                     "client is not connected");
    if (!core::is_valid_local_id(channel) || message.empty() || message.size() > 64U * 1024U)
        return core::Status::failure("authoritative_server.invalid_chat",
                                     "chat channel or message is invalid");
    auto status = logs_.append_chat(
        player->profile.player_uuid, std::string(player->profile.current_display_name()), channel,
        message, world_time, calendar(world_time), config_.server_session);
    if (!status)
        return status;
    const auto payload =
        std::string(player->profile.current_display_name()) + "|" + channel + "|" + message;
    for (const auto& [_, recipient] : players_) {
        status = host_.send_replication_message(
            recipient.client_id, profile_message("chat", payload, next_outbound_sequence_++));
        if (!status)
            return status;
    }
    return core::Status::ok();
}

core::Status AuthoritativeServer::flush_dirty_profiles() {
    for (auto& [_, player] : players_) {
        if (!player.dirty)
            continue;
        auto status = profiles_.save(player.profile);
        if (!status)
            return status;
        player.dirty = false;
    }
    return core::Status::ok();
}

core::Result<net::HostSessionTickResult>
AuthoritativeServer::tick(const net::ServerCommandDispatcher& dispatcher,
                          net::CommandExecutionContext context) {
    auto result = host_.tick(dispatcher, context);
    if (!result)
        return result;
    const auto world_time = context.world_state == nullptr ? simulation::WorldTick{0}
                                                           : context.world_state->world_time();
    for (const auto& report : result.value().command_reports) {
        auto status = audit_command(report, world_time);
        if (!status)
            return core::Result<net::HostSessionTickResult>::failure(status.error().code,
                                                                     status.error().message);
    }
    return result;
}

ConnectedPlayer* AuthoritativeServer::find(core::NetId client_id) noexcept {
    auto found = players_.find(client_id.value());
    return found == players_.end() ? nullptr : &found->second;
}
const ConnectedPlayer* AuthoritativeServer::find(core::NetId client_id) const noexcept {
    auto found = players_.find(client_id.value());
    return found == players_.end() ? nullptr : &found->second;
}
ConnectedPlayer* AuthoritativeServer::find(const player_profiles::PlayerUuid& uuid) noexcept {
    const auto found = std::ranges::find_if(
        players_, [&uuid](auto& entry) { return entry.second.profile.player_uuid == uuid; });
    return found == players_.end() ? nullptr : &found->second;
}
const ConnectedPlayer*
AuthoritativeServer::find(const player_profiles::PlayerUuid& uuid) const noexcept {
    const auto found = std::ranges::find_if(
        players_, [&uuid](const auto& entry) { return entry.second.profile.player_uuid == uuid; });
    return found == players_.end() ? nullptr : &found->second;
}

core::Status AuthoritativeServer::replace_profile(player_profiles::PlayerProfile profile,
                                                  bool notify_connected) {
    auto status = profile.validate();
    if (!status)
        return status;
    status = profiles_.save(profile);
    if (!status)
        return status;
    if (auto* connected = find(profile.player_uuid)) {
        connected->profile = std::move(profile);
        connected->dirty = false;
        if (notify_connected)
            return send_profile_snapshot(*connected);
    }
    return core::Status::ok();
}
net::HostSession& AuthoritativeServer::host_session() noexcept {
    return host_;
}
const player_profiles::FilePlayerProfileStore& AuthoritativeServer::profile_store() const noexcept {
    return profiles_;
}
server_logs::FileServerLog& AuthoritativeServer::logs() noexcept {
    return logs_;
}

const std::filesystem::path& AuthoritativeServer::world_root() const noexcept {
    return config_.world_root;
}

const std::filesystem::path& AuthoritativeServer::server_root() const noexcept {
    return logs_.server_root();
}

std::string_view AuthoritativeServer::server_session() const noexcept {
    return config_.server_session;
}

void AuthoritativeServer::set_ban_validator(JoinBanValidator validator) {
    config_.ban_validator = std::move(validator);
}

BanValidatorHandle AuthoritativeServer::add_ban_validator(JoinBanValidator validator) {
    auto handle = next_ban_validator_handle_++;
    while (handle == 0 || ban_validators_.contains(handle)) {
        handle = next_ban_validator_handle_++;
    }
    ban_validators_.emplace(handle, std::move(validator));
    return handle;
}

void AuthoritativeServer::remove_ban_validator(BanValidatorHandle handle) noexcept {
    ban_validators_.erase(handle);
}

core::Status AuthoritativeServer::send_profile_snapshot(const ConnectedPlayer& player) {
    auto status = host_.send_replication_message(
        player.client_id,
        profile_message("player_profile",
                        player_profiles::PlayerProfileTextCodec::encode(player.profile),
                        next_outbound_sequence_++));
    if (!status)
        return status;
    return host_.send_replication_message(
        player.client_id, profile_message("map_discovery",
                                          player_profiles::MapDiscoveryTextCodec::encode(
                                              player.profile.map_discovery),
                                          next_outbound_sequence_++));
}

core::Status AuthoritativeServer::send_map_update(const ConnectedPlayer& player,
                                                  std::string_view layer_id,
                                                  player_profiles::MapDiscoveryRegionCoord region) {
    const auto* source = player.profile.map_discovery.find_region(layer_id, region);
    if (source == nullptr)
        return core::Status::failure("authoritative_server.missing_map_region",
                                     "discovery region is missing after update");
    player_profiles::MapDiscovery delta;
    auto status = delta.upsert_region(*source);
    if (!status)
        return status;
    return host_.send_replication_message(
        player.client_id, profile_message("map_discovery_delta",
                                          player_profiles::MapDiscoveryTextCodec::encode(delta),
                                          next_outbound_sequence_++));
}

std::string AuthoritativeServer::calendar(simulation::WorldTick world_time) const {
    auto per_hour = config_.world_time.ticks_per_hour();
    auto per_day = config_.world_time.ticks_per_day();
    if (!per_hour || !per_day)
        return "invalid";
    const auto day = world_time / per_day.value() + 1;
    const auto within_day = world_time % per_day.value();
    const auto hour = within_day / per_hour.value();
    const auto minute =
        (within_day % per_hour.value()) * config_.world_time.minutes_per_hour / per_hour.value();
    std::ostringstream output;
    output << "Day " << day << ' ' << std::setw(2) << std::setfill('0') << hour << ':'
           << std::setw(2) << minute;
    return output.str();
}

core::Status AuthoritativeServer::audit_command(const net::HostSessionCommandReport& report,
                                                simulation::WorldTick world_time) {
    server_logs::ServerLogEntry entry;
    entry.real_timestamp_utc = server_logs::utc_now_iso8601();
    entry.world_time = world_time;
    entry.world_calendar = calendar(world_time);
    entry.event_type = report.success ? "server_command" : "server_command_failed";
    entry.server_session = config_.server_session;
    if (const auto* player = find(report.client_id)) {
        entry.player_uuid = player->profile.player_uuid;
        entry.display_name = std::string(player->profile.current_display_name());
    }
    entry.message = report.command_type;
    entry.metadata.emplace("sequence", std::to_string(report.sequence));
    if (!report.error_code.empty())
        entry.metadata.emplace("error", report.error_code);
    return logs_.append(server_logs::ServerLogCategory::audit, std::move(entry));
}

std::string hash_client_address(std::string_view address) {
    return "stable64:" + core::stable_hash64_hex(address);
}

} // namespace heartstead::server
