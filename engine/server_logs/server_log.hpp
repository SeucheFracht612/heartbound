#pragma once

#include "engine/core/result.hpp"
#include "engine/player_profiles/player_profile.hpp"
#include "engine/simulation/world_time.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace heartstead::server_logs {

inline constexpr std::size_t server_log_max_query_bytes = 512U * 1024U * 1024U;

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

    [[nodiscard]] core::Status validate() const;
    [[nodiscard]] bool matches(const ServerLogEntry& entry) const noexcept;
};

struct ServerLogConfig {
    std::uintmax_t rotate_after_bytes = 16U * 1024U * 1024U;
    bool rotate_daily = true;

    [[nodiscard]] core::Status validate() const;
};

struct ServerLogArchiveDecodeOptions {
    std::size_t maximum_output_bytes = server_log_max_query_bytes;
};

class ServerLogArchiveCodec {
  public:
    [[nodiscard]] static std::vector<std::uint8_t> encode(std::string_view text);
    [[nodiscard]] static core::Result<std::string>
    decode(std::span<const std::uint8_t> bytes, ServerLogArchiveDecodeOptions options = {});
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
    [[nodiscard]] core::Status initialize();
    [[nodiscard]] core::Status append(ServerLogCategory category, ServerLogEntry entry);
    [[nodiscard]] core::Result<std::vector<ServerLogEntry>>
    query_current(ServerLogCategory category, const ServerLogFilter& filter = {}) const;
    [[nodiscard]] core::Result<std::vector<ServerLogEntry>>
    query_all(ServerLogCategory category, const ServerLogFilter& filter = {}) const;

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
                                                std::size_t incoming_bytes,
                                                std::string_view incoming_day);

    std::filesystem::path server_root_;
    ServerLogConfig config_;
    mutable std::mutex mutex_;
    std::uint64_t rotation_sequence_ = 1;
    std::map<ServerLogCategory, std::string> current_days_;
};

[[nodiscard]] std::string utc_now_iso8601();
[[nodiscard]] std::string_view server_log_category_name(ServerLogCategory category) noexcept;

} // namespace heartstead::server_logs
