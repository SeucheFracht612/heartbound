#include "engine/server_logs/server_log.hpp"

#include "engine/core/ids.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <utility>

namespace heartstead::server_logs {

namespace {

constexpr std::string_view line_version = "v1";
constexpr std::size_t max_log_text_bytes = 64U * 1024U;
constexpr std::size_t max_encoded_log_line_bytes = 1024U * 1024U;
constexpr std::uintmax_t max_query_file_bytes = 512U * 1024U * 1024U;

[[nodiscard]] bool is_hex(char value) noexcept {
    return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f') ||
           (value >= 'A' && value <= 'F');
}

[[nodiscard]] unsigned char hex_value(char value) noexcept {
    if (value >= '0' && value <= '9') {
        return static_cast<unsigned char>(value - '0');
    }
    if (value >= 'a' && value <= 'f') {
        return static_cast<unsigned char>(10 + value - 'a');
    }
    return static_cast<unsigned char>(10 + value - 'A');
}

[[nodiscard]] std::string percent_escape(std::string_view input) {
    constexpr char hex[] = "0123456789ABCDEF";
    std::string result;
    result.reserve(input.size());
    for (const auto character : input) {
        const auto byte = static_cast<unsigned char>(character);
        if (character == '%' || character == '|' || character == '=' || character == ',' ||
            byte < 0x20U || byte == 0x7FU) {
            result.push_back('%');
            result.push_back(hex[(byte >> 4U) & 0x0FU]);
            result.push_back(hex[byte & 0x0FU]);
        } else {
            result.push_back(character);
        }
    }
    return result;
}

[[nodiscard]] core::Result<std::string> percent_unescape(std::string_view input) {
    std::string result;
    result.reserve(input.size());
    for (std::size_t index = 0; index < input.size(); ++index) {
        if (input[index] != '%') {
            result.push_back(input[index]);
            continue;
        }
        if (index + 2 >= input.size() || !is_hex(input[index + 1]) || !is_hex(input[index + 2])) {
            return core::Result<std::string>::failure("server_log.invalid_escape",
                                                      "server log line has an invalid escape");
        }
        result.push_back(
            static_cast<char>((hex_value(input[index + 1]) << 4U) | hex_value(input[index + 2])));
        index += 2;
    }
    return core::Result<std::string>::success(std::move(result));
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

[[nodiscard]] core::Result<std::uint64_t> parse_u64(std::string_view value,
                                                    std::string_view label) {
    std::uint64_t parsed = 0;
    const auto [ptr, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (error != std::errc{} || ptr != value.data() + value.size()) {
        return core::Result<std::uint64_t>::failure(
            "server_log.invalid_number", "invalid server log number: " + std::string(label));
    }
    return core::Result<std::uint64_t>::success(parsed);
}

[[nodiscard]] bool valid_utc_timestamp(std::string_view value) noexcept {
    if (value.size() != 24 || value[4] != '-' || value[7] != '-' || value[10] != 'T' ||
        value[13] != ':' || value[16] != ':' || value[19] != '.' || value[23] != 'Z') {
        return false;
    }
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (index == 4 || index == 7 || index == 10 || index == 13 || index == 16 || index == 19 ||
            index == 23) {
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
    const auto year_value = decimal(0, 4);
    const auto month_value = decimal(5, 2);
    const auto day_value = decimal(8, 2);
    const std::chrono::year_month_day calendar_date{
        std::chrono::year(static_cast<int>(year_value)),
        std::chrono::month(month_value),
        std::chrono::day(day_value),
    };
    return calendar_date.ok() && decimal(11, 2) <= 23U && decimal(14, 2) <= 59U &&
           decimal(17, 2) <= 59U && decimal(20, 3) <= 999U;
}

[[nodiscard]] bool valid_free_text(std::string_view value) noexcept {
    return value.size() <= max_log_text_bytes;
}

[[nodiscard]] std::string encode_metadata(const std::map<std::string, std::string>& metadata) {
    std::string result;
    for (const auto& [key, value] : metadata) {
        if (!result.empty()) {
            result.push_back(',');
        }
        result += percent_escape(key);
        result.push_back('=');
        result += percent_escape(value);
    }
    return result;
}

[[nodiscard]] core::Result<std::map<std::string, std::string>>
decode_metadata(std::string_view value) {
    std::map<std::string, std::string> result;
    if (value.empty()) {
        return core::Result<std::map<std::string, std::string>>::success(std::move(result));
    }
    for (const auto field : split(value, ',')) {
        const auto separator = field.find('=');
        if (separator == std::string_view::npos) {
            return core::Result<std::map<std::string, std::string>>::failure(
                "server_log.invalid_metadata", "server log metadata field is malformed");
        }
        auto key = percent_unescape(field.substr(0, separator));
        auto field_value = percent_unescape(field.substr(separator + 1));
        if (!key || !field_value ||
            !result.emplace(std::move(key).value(), std::move(field_value).value()).second) {
            return core::Result<std::map<std::string, std::string>>::failure(
                "server_log.invalid_metadata", "server log metadata is invalid or duplicated");
        }
    }
    return core::Result<std::map<std::string, std::string>>::success(std::move(result));
}

[[nodiscard]] std::string compact_timestamp(std::string_view utc) {
    std::string result;
    for (const auto character : utc) {
        if ((character >= '0' && character <= '9') || character == 'T' || character == 'Z') {
            result.push_back(character);
        }
    }
    return result;
}

} // namespace

core::Status ServerLogEntry::validate() const {
    if (!valid_utc_timestamp(real_timestamp_utc)) {
        return core::Status::failure(
            "server_log.invalid_real_timestamp",
            "server log real timestamp must be UTC ISO-8601 with milliseconds");
    }
    if (!core::is_valid_local_id(event_type)) {
        return core::Status::failure("server_log.invalid_event_type",
                                     "server log event type must be a valid local id");
    }
    if (server_session.empty() || server_session.size() > 128) {
        return core::Status::failure("server_log.invalid_session",
                                     "server log session id must be 1..128 bytes");
    }
    if (player_uuid.has_value() && !player_uuid->is_valid()) {
        return core::Status::failure("server_log.invalid_player_uuid",
                                     "server log player UUID is invalid");
    }
    if (!valid_free_text(world_calendar) || !valid_free_text(display_name) ||
        !valid_free_text(message_channel) || !valid_free_text(message)) {
        return core::Status::failure("server_log.text_too_large",
                                     "server log text field exceeds 64 KiB");
    }
    if (!message_channel.empty() && !core::is_valid_local_id(message_channel)) {
        return core::Status::failure("server_log.invalid_channel",
                                     "server log message channel must be a valid local id");
    }
    for (const auto& [key, value] : metadata) {
        if (!core::is_valid_local_id(key) || !valid_free_text(value)) {
            return core::Status::failure("server_log.invalid_metadata",
                                         "server log metadata key or value is invalid");
        }
    }
    return core::Status::ok();
}

bool ServerLogFilter::matches(const ServerLogEntry& entry) const noexcept {
    if (real_time_from_utc.has_value() && entry.real_timestamp_utc < *real_time_from_utc) {
        return false;
    }
    if (real_time_to_utc.has_value() && entry.real_timestamp_utc > *real_time_to_utc) {
        return false;
    }
    if (world_time_from.has_value() && entry.world_time < *world_time_from) {
        return false;
    }
    if (world_time_to.has_value() && entry.world_time > *world_time_to) {
        return false;
    }
    if (player_uuid.has_value() && entry.player_uuid != player_uuid) {
        return false;
    }
    if (!display_name.empty() && entry.display_name != display_name) {
        return false;
    }
    if (!event_type.empty() && entry.event_type != event_type) {
        return false;
    }
    if (!server_session.empty() && entry.server_session != server_session) {
        return false;
    }
    return message_contains.empty() || entry.message.find(message_contains) != std::string::npos;
}

std::string ServerLogLineCodec::encode(const ServerLogEntry& entry) {
    std::ostringstream output;
    output << line_version << '|' << entry.real_timestamp_utc << '|' << entry.world_time << '|'
           << percent_escape(entry.world_calendar) << '|' << percent_escape(entry.event_type) << '|'
           << percent_escape(entry.server_session) << '|'
           << (entry.player_uuid.has_value() ? entry.player_uuid->value() : "") << '|'
           << percent_escape(entry.display_name) << '|' << percent_escape(entry.message_channel)
           << '|' << percent_escape(entry.message) << '|' << encode_metadata(entry.metadata);
    return output.str();
}

core::Result<ServerLogEntry> ServerLogLineCodec::decode(std::string_view line) {
    if (!line.empty() && line.back() == '\r') {
        line.remove_suffix(1);
    }
    const auto fields = split(line, '|');
    if (fields.size() != 11 || fields[0] != line_version) {
        return core::Result<ServerLogEntry>::failure("server_log.invalid_line",
                                                     "server log line has an invalid shape");
    }
    auto world_time = parse_u64(fields[2], "world_time");
    auto calendar = percent_unescape(fields[3]);
    auto event = percent_unescape(fields[4]);
    auto session = percent_unescape(fields[5]);
    auto display_name = percent_unescape(fields[7]);
    auto channel = percent_unescape(fields[8]);
    auto message = percent_unescape(fields[9]);
    auto metadata = decode_metadata(fields[10]);
    if (!world_time || !calendar || !event || !session || !display_name || !channel || !message ||
        !metadata) {
        return core::Result<ServerLogEntry>::failure("server_log.invalid_line",
                                                     "server log line contains invalid fields");
    }

    ServerLogEntry entry;
    entry.real_timestamp_utc = std::string(fields[1]);
    entry.world_time = world_time.value();
    entry.world_calendar = std::move(calendar).value();
    entry.event_type = std::move(event).value();
    entry.server_session = std::move(session).value();
    if (!fields[6].empty()) {
        auto uuid = player_profiles::PlayerUuid::parse(fields[6]);
        if (!uuid) {
            return core::Result<ServerLogEntry>::failure("server_log.invalid_player_uuid",
                                                         "server log player UUID is invalid");
        }
        entry.player_uuid = *uuid;
    }
    entry.display_name = std::move(display_name).value();
    entry.message_channel = std::move(channel).value();
    entry.message = std::move(message).value();
    entry.metadata = std::move(metadata).value();
    auto status = entry.validate();
    if (!status) {
        return core::Result<ServerLogEntry>::failure(status.error().code, status.error().message);
    }
    return core::Result<ServerLogEntry>::success(std::move(entry));
}

FileServerLog::FileServerLog(std::filesystem::path server_root, ServerLogConfig config)
    : server_root_(std::move(server_root)), config_(config) {}

const std::filesystem::path& FileServerLog::server_root() const noexcept {
    return server_root_;
}

std::filesystem::path FileServerLog::current_path(ServerLogCategory category) const {
    switch (category) {
    case ServerLogCategory::general:
        return server_root_ / "logs" / "current.log";
    case ServerLogCategory::chat:
        return server_root_ / "logs" / "chat" / "current.log";
    case ServerLogCategory::audit:
        return server_root_ / "logs" / "audit" / "current.log";
    }
    return server_root_ / "logs" / "current.log";
}

core::Status FileServerLog::append(ServerLogCategory category, ServerLogEntry entry) {
    if (entry.real_timestamp_utc.empty()) {
        entry.real_timestamp_utc = utc_now_iso8601();
    }
    auto status = entry.validate();
    if (!status) {
        return status;
    }
    auto line = ServerLogLineCodec::encode(entry);
    line.push_back('\n');
    if (line.size() > max_encoded_log_line_bytes) {
        return core::Status::failure("server_log.line_too_large",
                                     "encoded server log entry exceeds one MiB");
    }

    std::scoped_lock lock(mutex_);
    status = rotate_if_needed(category, line.size());
    if (!status) {
        return status;
    }
    const auto path = current_path(category);
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        return core::Status::failure("server_log.create_directory_failed", error.message());
    }
    std::ofstream output(path, std::ios::binary | std::ios::app);
    if (!output) {
        return core::Status::failure("server_log.open_failed", "failed to open server log");
    }
    output.write(line.data(), static_cast<std::streamsize>(line.size()));
    output.flush();
    if (!output) {
        return core::Status::failure("server_log.write_failed", "failed to append server log");
    }
    return core::Status::ok();
}

core::Result<std::vector<ServerLogEntry>>
FileServerLog::query_current(ServerLogCategory category, const ServerLogFilter& filter) const {
    std::scoped_lock lock(mutex_);
    std::vector<ServerLogEntry> result;
    const auto path = current_path(category);
    std::error_code error;
    const auto exists = std::filesystem::exists(path, error);
    if (error) {
        return core::Result<std::vector<ServerLogEntry>>::failure("server_log.read_failed",
                                                                  error.message());
    }
    if (!exists) {
        return core::Result<std::vector<ServerLogEntry>>::success(std::move(result));
    }
    const auto file_bytes = std::filesystem::file_size(path, error);
    if (error) {
        return core::Result<std::vector<ServerLogEntry>>::failure("server_log.read_failed",
                                                                  error.message());
    }
    if (file_bytes > max_query_file_bytes) {
        return core::Result<std::vector<ServerLogEntry>>::failure(
            "server_log.file_too_large", "current server log exceeds the query size limit");
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return core::Result<std::vector<ServerLogEntry>>::failure(
            "server_log.open_failed", "failed to open current server log");
    }
    std::string line;
    while (std::getline(input, line)) {
        if (line.size() > max_encoded_log_line_bytes) {
            return core::Result<std::vector<ServerLogEntry>>::failure(
                "server_log.line_too_large", "server log line exceeds one MiB");
        }
        auto entry = ServerLogLineCodec::decode(line);
        if (!entry) {
            return core::Result<std::vector<ServerLogEntry>>::failure(entry.error().code,
                                                                      entry.error().message);
        }
        if (filter.matches(entry.value())) {
            result.push_back(std::move(entry).value());
        }
    }
    if (!input.eof()) {
        return core::Result<std::vector<ServerLogEntry>>::failure(
            "server_log.read_failed", "failed while reading current server log");
    }
    return core::Result<std::vector<ServerLogEntry>>::success(std::move(result));
}

core::Status FileServerLog::append_join(const player_profiles::PlayerUuid& player_uuid,
                                        std::string display_name, std::string address_hash,
                                        simulation::WorldTick world_time,
                                        std::string world_calendar, std::string server_session) {
    ServerLogEntry entry;
    entry.world_time = world_time;
    entry.world_calendar = std::move(world_calendar);
    entry.event_type = "join";
    entry.server_session = std::move(server_session);
    entry.player_uuid = player_uuid;
    entry.display_name = std::move(display_name);
    entry.metadata.emplace("address_hash", std::move(address_hash));
    return append(ServerLogCategory::general, std::move(entry));
}

core::Status FileServerLog::append_leave(const player_profiles::PlayerUuid& player_uuid,
                                         std::string display_name, simulation::WorldTick world_time,
                                         std::string world_calendar, std::string server_session) {
    ServerLogEntry entry;
    entry.world_time = world_time;
    entry.world_calendar = std::move(world_calendar);
    entry.event_type = "leave";
    entry.server_session = std::move(server_session);
    entry.player_uuid = player_uuid;
    entry.display_name = std::move(display_name);
    return append(ServerLogCategory::general, std::move(entry));
}

core::Status FileServerLog::append_chat(const player_profiles::PlayerUuid& player_uuid,
                                        std::string display_name, std::string channel,
                                        std::string message, simulation::WorldTick world_time,
                                        std::string world_calendar, std::string server_session,
                                        std::map<std::string, std::string> moderation_metadata) {
    ServerLogEntry entry;
    entry.world_time = world_time;
    entry.world_calendar = std::move(world_calendar);
    entry.event_type = "chat";
    entry.server_session = std::move(server_session);
    entry.player_uuid = player_uuid;
    entry.display_name = std::move(display_name);
    entry.message_channel = std::move(channel);
    entry.message = std::move(message);
    entry.metadata = std::move(moderation_metadata);
    return append(ServerLogCategory::chat, std::move(entry));
}

core::Status FileServerLog::rotate_if_needed(ServerLogCategory category,
                                             std::size_t incoming_bytes) {
    if (config_.rotate_after_bytes == 0) {
        return core::Status::failure("server_log.invalid_rotation_size",
                                     "server log rotation size must be non-zero");
    }
    const auto current = current_path(category);
    std::error_code error;
    const auto exists = std::filesystem::exists(current, error);
    if (error) {
        return core::Status::failure("server_log.stat_failed", error.message());
    }
    if (!exists) {
        return core::Status::ok();
    }
    const auto size = std::filesystem::file_size(current, error);
    if (error) {
        return core::Status::failure("server_log.stat_failed", error.message());
    }
    if (size < config_.rotate_after_bytes && incoming_bytes <= config_.rotate_after_bytes - size) {
        return core::Status::ok();
    }

    const auto rotated_dir = server_root_ / "logs" / "rotated";
    std::filesystem::create_directories(rotated_dir, error);
    if (error) {
        return core::Status::failure("server_log.create_directory_failed", error.message());
    }
    const auto filename = compact_timestamp(utc_now_iso8601()) + "." +
                          std::string(server_log_category_name(category)) + "." +
                          std::to_string(rotation_sequence_++) + ".log";
    std::filesystem::rename(current, rotated_dir / filename, error);
    if (error) {
        return core::Status::failure("server_log.rotate_failed", error.message());
    }
    return core::Status::ok();
}

std::string utc_now_iso8601() {
    const auto now = std::chrono::system_clock::now();
    const auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &time);
#else
    gmtime_r(&time, &utc);
#endif
    std::ostringstream output;
    output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S") << '.' << std::setw(3) << std::setfill('0')
           << milliseconds.count() << 'Z';
    return output.str();
}

std::string_view server_log_category_name(ServerLogCategory category) noexcept {
    switch (category) {
    case ServerLogCategory::general:
        return "server";
    case ServerLogCategory::chat:
        return "chat";
    case ServerLogCategory::audit:
        return "audit";
    }
    return "unknown";
}

} // namespace heartstead::server_logs
