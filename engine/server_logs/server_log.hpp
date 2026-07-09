#pragma once

#include "engine/core/result.hpp"
#include "engine/player_profiles/player_profile.hpp"
#include "engine/simulation/world_time.hpp"

#include <cstddef>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace heartstead::server_logs {

enum class ServerLogCategory {
    general,
    chat,
    audit,
};

struct ServerLogEntry {
    std::string real_timestamp_utc;
    simulation::WorldTick world_time = 0;
    std::string world_calendar;
    std::string event_type;
    std::string server_session;
    std::optional<player_profiles::PlayerUuid> player_uuid;
    std::string display_name;
    std::string message_channel;
    std::string message;
    std::map<std::string, std::string> metadata;

    [[nodiscard]] core::Status validate() const;
};

struct ServerLogFilter {
    std::optional<std::string> real_time_from_utc;
    std::optional<std::string> real_time_to_utc;
    std::optional<simulation::WorldTick> world_time_from;
    std::optional<simulation::WorldTick> world_time_to;
    std::optional<player_profiles::PlayerUuid> player_uuid;
    std::string display_name;
    std::string event_type;
    std::string message_contains;
    std::string server_session;

    [[nodiscard]] bool matches(const ServerLogEntry& entry) const noexcept;
};

struct ServerLogConfig {
    std::uintmax_t rotate_after_bytes = 16U * 1024U * 1024U;
};

class ServerLogLineCodec {
  public:
    [[nodiscard]] static std::string encode(const ServerLogEntry& entry);
    [[nodiscard]] static core::Result<ServerLogEntry> decode(std::string_view line);
};

class FileServerLog {
  public:
    explicit FileServerLog(std::filesystem::path server_root, ServerLogConfig config = {});

    [[nodiscard]] const std::filesystem::path& server_root() const noexcept;
    [[nodiscard]] std::filesystem::path current_path(ServerLogCategory category) const;
    [[nodiscard]] core::Status append(ServerLogCategory category, ServerLogEntry entry);
    [[nodiscard]] core::Result<std::vector<ServerLogEntry>>
    query_current(ServerLogCategory category, const ServerLogFilter& filter = {}) const;

    [[nodiscard]] core::Status append_join(const player_profiles::PlayerUuid& player_uuid,
                                           std::string display_name, std::string address_hash,
                                           simulation::WorldTick world_time,
                                           std::string world_calendar, std::string server_session);
    [[nodiscard]] core::Status append_leave(const player_profiles::PlayerUuid& player_uuid,
                                            std::string display_name,
                                            simulation::WorldTick world_time,
                                            std::string world_calendar, std::string server_session);
    [[nodiscard]] core::Status
    append_chat(const player_profiles::PlayerUuid& player_uuid, std::string display_name,
                std::string channel, std::string message, simulation::WorldTick world_time,
                std::string world_calendar, std::string server_session,
                std::map<std::string, std::string> moderation_metadata = {});

  private:
    [[nodiscard]] core::Status rotate_if_needed(ServerLogCategory category,
                                                std::size_t incoming_bytes);

    std::filesystem::path server_root_;
    ServerLogConfig config_;
    mutable std::mutex mutex_;
    std::uint64_t rotation_sequence_ = 1;
};

[[nodiscard]] std::string utc_now_iso8601();
[[nodiscard]] std::string_view server_log_category_name(ServerLogCategory category) noexcept;

} // namespace heartstead::server_logs
