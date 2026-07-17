#include "engine/server_logs/server_log.hpp"

#include "engine/core/file_io.hpp"
#include "engine/core/filesystem.hpp"
#include "engine/core/ids.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <system_error>
#include <tuple>
#include <utility>

namespace heartstead::server_logs {

namespace {

constexpr std::string_view line_version = "v1";
constexpr std::size_t max_log_text_bytes = 64U * 1024U;
constexpr std::size_t max_encoded_log_line_bytes = 1024U * 1024U;
constexpr std::size_t max_query_file_bytes = server_log_max_query_bytes;
constexpr std::size_t max_log_metadata_fields = 256;
constexpr std::size_t max_query_entries = 1'000'000;
constexpr std::size_t max_query_archives = 4096;
constexpr std::size_t max_rotated_directory_entries = (max_query_archives * 3U) + 32U;
constexpr std::size_t max_rotation_index_bytes = 4U * 1024U * 1024U;
constexpr std::string_view archive_magic = "HSLZ1";

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

[[nodiscard]] bool add_raw_size(std::size_t& total, std::size_t amount,
                                std::size_t limit) noexcept {
    if (total > limit || amount > limit - total) {
        return false;
    }
    total += amount;
    return true;
}

[[nodiscard]] bool add_percent_escaped_size(std::size_t& total, std::string_view input,
                                            std::size_t limit) noexcept {
    for (const auto character : input) {
        const auto byte = static_cast<unsigned char>(character);
        const auto amount = character == '%' || character == '|' || character == '=' ||
                                    character == ',' || byte < 0x20U || byte == 0x7FU
                                ? 3U
                                : 1U;
        if (!add_raw_size(total, amount, limit)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::size_t decimal_digits(std::uint64_t value) noexcept {
    std::size_t digits = 1;
    while (value >= 10U) {
        value /= 10U;
        ++digits;
    }
    return digits;
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
    if (static_cast<std::size_t>(std::ranges::count(value, ',')) >= max_log_metadata_fields) {
        return core::Result<std::map<std::string, std::string>>::failure(
            "server_log.invalid_metadata", "server log metadata has too many fields");
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

[[nodiscard]] bool valid_category(ServerLogCategory category) noexcept {
    switch (category) {
    case ServerLogCategory::general:
    case ServerLogCategory::chat:
    case ServerLogCategory::audit:
        return true;
    }
    return false;
}

[[nodiscard]] std::optional<ServerLogCategory> category_from_name(std::string_view name) noexcept {
    if (name == "server") {
        return ServerLogCategory::general;
    }
    if (name == "chat") {
        return ServerLogCategory::chat;
    }
    if (name == "audit") {
        return ServerLogCategory::audit;
    }
    return std::nullopt;
}

[[nodiscard]] bool valid_compact_timestamp(std::string_view value) noexcept {
    if (value.size() != 19 || value[8] != 'T' || value[18] != 'Z') {
        return false;
    }
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (index == 8 || index == 18) {
            continue;
        }
        if (value[index] < '0' || value[index] > '9') {
            return false;
        }
    }
    std::string expanded;
    expanded.reserve(24);
    expanded.append(value.substr(0, 4));
    expanded.push_back('-');
    expanded.append(value.substr(4, 2));
    expanded.push_back('-');
    expanded.append(value.substr(6, 2));
    expanded.push_back('T');
    expanded.append(value.substr(9, 2));
    expanded.push_back(':');
    expanded.append(value.substr(11, 2));
    expanded.push_back(':');
    expanded.append(value.substr(13, 2));
    expanded.push_back('.');
    expanded.append(value.substr(15, 3));
    expanded.push_back('Z');
    return valid_utc_timestamp(expanded);
}

struct ArchiveFile {
    std::filesystem::path path;
    std::string timestamp;
    ServerLogCategory category = ServerLogCategory::general;
    std::uint64_t sequence = 0;
};

[[nodiscard]] std::optional<ArchiveFile> parse_archive_filename(const std::filesystem::path& path) {
    constexpr std::string_view suffix = ".log.hsz";
    const auto filename = path.filename().string();
    if (!filename.ends_with(suffix)) {
        return std::nullopt;
    }
    const auto timestamp_end = filename.find('.');
    if (timestamp_end == std::string::npos) {
        return std::nullopt;
    }
    const auto category_end = filename.find('.', timestamp_end + 1U);
    if (category_end == std::string::npos) {
        return std::nullopt;
    }
    const auto sequence_end = filename.find('.', category_end + 1U);
    if (sequence_end == std::string::npos || filename.substr(sequence_end) != suffix) {
        return std::nullopt;
    }

    const std::string_view filename_view(filename);
    const auto timestamp = filename_view.substr(0, timestamp_end);
    const auto category_name =
        filename_view.substr(timestamp_end + 1U, category_end - timestamp_end - 1U);
    const auto sequence_text =
        filename_view.substr(category_end + 1U, sequence_end - category_end - 1U);
    const auto category = category_from_name(category_name);
    if (!valid_compact_timestamp(timestamp) || !category || sequence_text.empty() ||
        (sequence_text.size() > 1U && sequence_text.front() == '0')) {
        return std::nullopt;
    }
    std::uint64_t sequence = 0;
    const auto [end, conversion_error] = std::from_chars(
        sequence_text.data(), sequence_text.data() + sequence_text.size(), sequence);
    if (conversion_error != std::errc{} || end != sequence_text.data() + sequence_text.size() ||
        sequence == 0) {
        return std::nullopt;
    }
    return ArchiveFile{path, std::string(timestamp), *category, sequence};
}

[[nodiscard]] core::Result<std::vector<ArchiveFile>>
load_archive_catalog(const std::filesystem::path& rotated_directory,
                     std::string_view limit_error_code) {
    auto files = core::list_regular_files(rotated_directory,
                                          {.maximum_entries = max_rotated_directory_entries});
    if (!files) {
        const auto code =
            files.error().code == "core.directory_symlink_forbidden" ? "server_log.unsafe_symlink"
            : files.error().code == "core.directory_too_large"       ? std::string(limit_error_code)
                                                                     : "server_log.read_failed";
        return core::Result<std::vector<ArchiveFile>>::failure(code, files.error().message);
    }

    std::vector<ArchiveFile> archives;
    std::map<ServerLogCategory, std::size_t> archive_counts;
    for (const auto& path : files.value()) {
        if (path.extension() != ".hsz") {
            continue;
        }
        auto archive = parse_archive_filename(path);
        if (!archive) {
            return core::Result<std::vector<ArchiveFile>>::failure(
                "server_log.invalid_archive_name",
                "rotated log archive has an invalid filename: " + path.filename().string());
        }
        auto& count = archive_counts[archive->category];
        ++count;
        if (count > max_query_archives) {
            return core::Result<std::vector<ArchiveFile>>::failure(
                std::string(limit_error_code),
                "server log category exceeds its archive-count limit");
        }
        archives.push_back(std::move(*archive));
    }
    return core::Result<std::vector<ArchiveFile>>::success(std::move(archives));
}

[[nodiscard]] core::Result<bool> safe_directory_exists(const std::filesystem::path& path,
                                                       std::string_view label) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error == std::errc::no_such_file_or_directory ||
        (!error && status.type() == std::filesystem::file_type::not_found)) {
        return core::Result<bool>::success(false);
    }
    if (error) {
        return core::Result<bool>::failure("server_log.path_check_failed",
                                           "failed to inspect " + std::string(label) + ": " +
                                               error.message());
    }
    if (std::filesystem::is_symlink(status)) {
        return core::Result<bool>::failure("server_log.unsafe_symlink",
                                           std::string(label) +
                                               " must not be a symbolic link: " + path.string());
    }
    if (!std::filesystem::is_directory(status)) {
        return core::Result<bool>::failure("server_log.invalid_path",
                                           std::string(label) +
                                               " is not a directory: " + path.string());
    }
    return core::Result<bool>::success(true);
}

[[nodiscard]] core::Result<bool> safe_regular_file_exists(const std::filesystem::path& path,
                                                          std::string_view label) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error == std::errc::no_such_file_or_directory ||
        (!error && status.type() == std::filesystem::file_type::not_found)) {
        return core::Result<bool>::success(false);
    }
    if (error) {
        return core::Result<bool>::failure("server_log.path_check_failed",
                                           "failed to inspect " + std::string(label) + ": " +
                                               error.message());
    }
    if (std::filesystem::is_symlink(status)) {
        return core::Result<bool>::failure("server_log.unsafe_symlink",
                                           std::string(label) +
                                               " must not be a symbolic link: " + path.string());
    }
    if (!std::filesystem::is_regular_file(status)) {
        return core::Result<bool>::failure("server_log.invalid_path",
                                           std::string(label) +
                                               " is not a regular file: " + path.string());
    }
    return core::Result<bool>::success(true);
}

[[nodiscard]] core::Result<bool> safe_path_exists(const std::filesystem::path& path,
                                                  std::string_view label) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error == std::errc::no_such_file_or_directory ||
        (!error && status.type() == std::filesystem::file_type::not_found)) {
        return core::Result<bool>::success(false);
    }
    if (error) {
        return core::Result<bool>::failure("server_log.path_check_failed",
                                           "failed to inspect " + std::string(label) + ": " +
                                               error.message());
    }
    if (std::filesystem::is_symlink(status)) {
        return core::Result<bool>::failure("server_log.unsafe_symlink",
                                           std::string(label) +
                                               " must not be a symbolic link: " + path.string());
    }
    return core::Result<bool>::success(true);
}

[[nodiscard]] core::Status ensure_appendable_file(const std::filesystem::path& path,
                                                  std::string_view label) {
    auto exists = safe_regular_file_exists(path, label);
    if (!exists) {
        return core::Status::failure(exists.error().code, exists.error().message);
    }
    std::ofstream output(path, std::ios::binary | std::ios::app);
    if (!output) {
        return core::Status::failure("server_log.open_failed",
                                     "failed to open " + std::string(label) + ": " + path.string());
    }
    output.close();
    if (!output) {
        return core::Status::failure("server_log.write_failed", "failed to close " +
                                                                    std::string(label) + ": " +
                                                                    path.string());
    }
    return core::Status::ok();
}

[[nodiscard]] core::Status ensure_root_directory(const std::filesystem::path& root) {
    if (root.empty()) {
        return core::Status::failure("server_log.invalid_root",
                                     "server log root must not be empty");
    }
    auto exists = safe_directory_exists(root, "server log root");
    if (!exists) {
        return core::Status::failure(exists.error().code, exists.error().message);
    }
    if (!exists.value()) {
        std::error_code error;
        std::filesystem::create_directories(root, error);
        if (error) {
            return core::Status::failure("server_log.create_directory_failed", error.message());
        }
        exists = safe_directory_exists(root, "server log root");
        if (!exists || !exists.value()) {
            return !exists ? core::Status::failure(exists.error().code, exists.error().message)
                           : core::Status::failure("server_log.create_directory_failed",
                                                   "server log root was not created");
        }
    }
    return core::Status::ok();
}

[[nodiscard]] core::Status ensure_child_directory(const std::filesystem::path& path,
                                                  std::string_view label) {
    auto exists = safe_directory_exists(path, label);
    if (!exists) {
        return core::Status::failure(exists.error().code, exists.error().message);
    }
    if (exists.value()) {
        return core::Status::ok();
    }
    std::error_code error;
    (void)std::filesystem::create_directory(path, error);
    if (error) {
        return core::Status::failure("server_log.create_directory_failed",
                                     "failed to create " + std::string(label) + ": " +
                                         error.message());
    }
    exists = safe_directory_exists(path, label);
    if (!exists || !exists.value()) {
        return !exists ? core::Status::failure(exists.error().code, exists.error().message)
                       : core::Status::failure("server_log.create_directory_failed",
                                               std::string(label) + " was not created");
    }
    return core::Status::ok();
}

[[nodiscard]] std::filesystem::path category_directory(const std::filesystem::path& root,
                                                       ServerLogCategory category) {
    const auto logs = root / "logs";
    switch (category) {
    case ServerLogCategory::general:
        return logs;
    case ServerLogCategory::chat:
        return logs / "chat";
    case ServerLogCategory::audit:
        return logs / "audit";
    }
    return logs;
}

[[nodiscard]] core::Status ensure_category_directory(const std::filesystem::path& root,
                                                     ServerLogCategory category) {
    auto status = ensure_root_directory(root);
    if (!status) {
        return status;
    }
    status = ensure_child_directory(root / "logs", "server log directory");
    if (!status || category == ServerLogCategory::general) {
        return status;
    }
    return ensure_child_directory(category_directory(root, category),
                                  "server log category directory");
}

[[nodiscard]] core::Result<bool> category_directory_exists(const std::filesystem::path& root,
                                                           ServerLogCategory category) {
    if (root.empty()) {
        return core::Result<bool>::failure("server_log.invalid_root",
                                           "server log root must not be empty");
    }
    auto exists = safe_directory_exists(root, "server log root");
    if (!exists || !exists.value()) {
        return exists;
    }
    exists = safe_directory_exists(root / "logs", "server log directory");
    if (!exists || !exists.value() || category == ServerLogCategory::general) {
        return exists;
    }
    return safe_directory_exists(category_directory(root, category),
                                 "server log category directory");
}

[[nodiscard]] core::Status ensure_rotated_directory(const std::filesystem::path& root) {
    auto status = ensure_root_directory(root);
    if (!status) {
        return status;
    }
    status = ensure_child_directory(root / "logs", "server log directory");
    if (!status) {
        return status;
    }
    return ensure_child_directory(root / "logs" / "rotated", "rotated server log directory");
}

[[nodiscard]] core::Result<bool> rotated_directory_exists(const std::filesystem::path& root) {
    if (root.empty()) {
        return core::Result<bool>::failure("server_log.invalid_root",
                                           "server log root must not be empty");
    }
    auto exists = safe_directory_exists(root, "server log root");
    if (!exists || !exists.value()) {
        return exists;
    }
    exists = safe_directory_exists(root / "logs", "server log directory");
    if (!exists || !exists.value()) {
        return exists;
    }
    return safe_directory_exists(root / "logs" / "rotated", "rotated server log directory");
}

[[nodiscard]] core::Result<std::string> read_log_text(const std::filesystem::path& path,
                                                      std::size_t maximum_bytes,
                                                      std::string_view too_large_code,
                                                      std::string_view label) {
    auto text = core::read_text_file(path, {.maximum_bytes = maximum_bytes});
    if (!text) {
        return core::Result<std::string>::failure(
            text.error().code == "core.file_too_large" ? std::string(too_large_code)
                                                       : "server_log.read_failed",
            "failed to read " + std::string(label) + ": " + text.error().message);
    }
    return text;
}

[[nodiscard]] core::Result<std::vector<std::uint8_t>>
read_log_bytes(const std::filesystem::path& path, std::size_t maximum_bytes,
               std::string_view too_large_code, std::string_view label) {
    auto bytes = core::read_binary_file(path, {.maximum_bytes = maximum_bytes});
    if (!bytes) {
        return core::Result<std::vector<std::uint8_t>>::failure(
            bytes.error().code == "core.file_too_large" ? std::string(too_large_code)
                                                        : "server_log.read_failed",
            "failed to read " + std::string(label) + ": " + bytes.error().message);
    }
    return bytes;
}

[[nodiscard]] core::Status collect_log_text(std::string_view text, const ServerLogFilter& filter,
                                            std::vector<ServerLogEntry>& result,
                                            std::size_t& processed_bytes) {
    if (text.size() > max_query_file_bytes - std::min(max_query_file_bytes, processed_bytes)) {
        return core::Status::failure("server_log.query_too_large",
                                     "server log query exceeds its aggregate byte limit");
    }
    processed_bytes += text.size();
    std::size_t start = 0;
    while (start < text.size()) {
        const auto end = text.find('\n', start);
        const auto line =
            end == std::string_view::npos ? text.substr(start) : text.substr(start, end - start);
        if (line.empty()) {
            return core::Status::failure("server_log.invalid_line",
                                         "server log contains an empty record");
        }
        if (line.size() > max_encoded_log_line_bytes) {
            return core::Status::failure("server_log.line_too_large",
                                         "server log line exceeds one MiB");
        }
        auto entry = ServerLogLineCodec::decode(line);
        if (!entry) {
            return core::Status::failure(entry.error().code, entry.error().message);
        }
        if (filter.matches(entry.value())) {
            if (result.size() >= max_query_entries) {
                return core::Status::failure("server_log.query_too_large",
                                             "server log query exceeds its result-count limit");
            }
            result.push_back(std::move(entry).value());
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1U;
    }
    return core::Status::ok();
}

[[nodiscard]] core::Result<std::optional<ServerLogEntry>>
read_first_log_entry(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return core::Result<std::optional<ServerLogEntry>>::failure(
            "server_log.open_failed", "failed to open current server log");
    }
    std::string line;
    line.reserve(512);
    bool read_any_byte = false;
    char character = '\0';
    while (input.get(character)) {
        read_any_byte = true;
        if (character == '\n') {
            break;
        }
        if (line.size() >= max_encoded_log_line_bytes) {
            return core::Result<std::optional<ServerLogEntry>>::failure(
                "server_log.line_too_large", "server log line exceeds one MiB");
        }
        line.push_back(character);
    }
    if (input.bad()) {
        return core::Result<std::optional<ServerLogEntry>>::failure(
            "server_log.read_failed", "failed while reading current server log");
    }
    if (!read_any_byte) {
        return core::Result<std::optional<ServerLogEntry>>::success(std::nullopt);
    }
    auto entry = ServerLogLineCodec::decode(line);
    if (!entry) {
        return core::Result<std::optional<ServerLogEntry>>::failure(entry.error().code,
                                                                    entry.error().message);
    }
    return core::Result<std::optional<ServerLogEntry>>::success(
        std::optional<ServerLogEntry>{std::move(entry).value()});
}

[[nodiscard]] core::Status
write_rotation_index(const std::filesystem::path& rotated_directory,
                     std::string_view archive_filename, ServerLogCategory category,
                     std::string_view current_day, std::string_view incoming_day,
                     std::size_t source_bytes, std::size_t archive_bytes) {
    const auto index_path = rotated_directory / "index.log";
    auto index_exists = safe_regular_file_exists(index_path, "rotated server log index");
    if (!index_exists) {
        return core::Status::failure(index_exists.error().code, index_exists.error().message);
    }

    std::string index_text;
    if (index_exists.value()) {
        auto existing = read_log_text(index_path, max_rotation_index_bytes,
                                      "server_log.index_too_large", "rotated server log index");
        if (!existing) {
            return core::Status::failure(existing.error().code, existing.error().message);
        }
        index_text = std::move(existing).value();
        if (!index_text.empty() && index_text.back() != '\n') {
            return core::Status::failure("server_log.invalid_index",
                                         "rotated server log index has a partial final record");
        }
    }

    std::ostringstream row;
    row << archive_filename << '|' << server_log_category_name(category) << '|' << current_day
        << '|' << incoming_day << '|' << source_bytes << '|' << archive_bytes << '\n';
    const auto row_text = row.str();
    if (index_text.size() > max_rotation_index_bytes ||
        row_text.size() > max_rotation_index_bytes - index_text.size()) {
        return core::Status::failure("server_log.index_too_large",
                                     "rotated server log index exceeds four MiB");
    }
    index_text += row_text;

    const auto temporary =
        rotated_directory / ("index.log." + std::string(archive_filename) + ".tmp");
    auto temporary_exists = safe_path_exists(temporary, "temporary rotated server log index");
    if (!temporary_exists) {
        return core::Status::failure(temporary_exists.error().code,
                                     temporary_exists.error().message);
    }
    if (temporary_exists.value()) {
        return core::Status::failure("server_log.index_failed",
                                     "temporary rotated server log index already exists");
    }
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            return core::Status::failure("server_log.index_failed",
                                         "failed to create temporary rotated log index");
        }
        output.write(index_text.data(), static_cast<std::streamsize>(index_text.size()));
        output.close();
        if (!output) {
            std::error_code cleanup_error;
            std::filesystem::remove(temporary, cleanup_error);
            return core::Status::failure("server_log.index_failed",
                                         "failed to write temporary rotated log index");
        }
    }
    const auto replace_error = core::replace_file(temporary, index_path);
    if (replace_error) {
        std::error_code cleanup_error;
        std::filesystem::remove(temporary, cleanup_error);
        return core::Status::failure("server_log.index_failed", replace_error.message());
    }
    return core::Status::ok();
}

} // namespace

std::vector<std::uint8_t> ServerLogArchiveCodec::encode(std::string_view text) {
    std::vector<std::uint8_t> output(archive_magic.begin(), archive_magic.end());
    std::size_t index = 0;
    while (index < text.size()) {
        std::size_t run = 1;
        while (index + run < text.size() && text[index + run] == text[index] && run < 128)
            ++run;
        if (run >= 4) {
            output.push_back(static_cast<std::uint8_t>(0x80U | (run - 1U)));
            output.push_back(static_cast<std::uint8_t>(text[index]));
            index += run;
            continue;
        }
        const auto literal_start = index;
        index += run;
        while (index < text.size() && index - literal_start < 128) {
            std::size_t next_run = 1;
            while (index + next_run < text.size() && text[index + next_run] == text[index] &&
                   next_run < 128) {
                ++next_run;
            }
            if (next_run >= 4 || index - literal_start + next_run > 128)
                break;
            index += next_run;
        }
        const auto length = index - literal_start;
        output.push_back(static_cast<std::uint8_t>(length - 1U));
        output.insert(output.end(), text.begin() + static_cast<std::ptrdiff_t>(literal_start),
                      text.begin() + static_cast<std::ptrdiff_t>(index));
    }
    return output;
}

core::Result<std::string> ServerLogArchiveCodec::decode(std::span<const std::uint8_t> bytes,
                                                        ServerLogArchiveDecodeOptions options) {
    if (bytes.size() < archive_magic.size() ||
        !std::equal(archive_magic.begin(), archive_magic.end(), bytes.begin())) {
        return core::Result<std::string>::failure("server_log.invalid_archive",
                                                  "rotated log archive magic is invalid");
    }
    std::string output;
    std::size_t index = archive_magic.size();
    while (index < bytes.size()) {
        const auto token = bytes[index++];
        const auto length = static_cast<std::size_t>((token & 0x7FU) + 1U);
        if (output.size() > options.maximum_output_bytes ||
            length > options.maximum_output_bytes - output.size()) {
            return core::Result<std::string>::failure("server_log.archive_too_large",
                                                      "rotated log expands beyond query limit");
        }
        if ((token & 0x80U) != 0) {
            if (index >= bytes.size()) {
                return core::Result<std::string>::failure("server_log.truncated_archive",
                                                          "rotated log run is truncated");
            }
            output.append(length, static_cast<char>(bytes[index++]));
        } else {
            if (length > bytes.size() - index) {
                return core::Result<std::string>::failure("server_log.truncated_archive",
                                                          "rotated log literal is truncated");
            }
            output.append(reinterpret_cast<const char*>(bytes.data() + index), length);
            index += length;
        }
    }
    return core::Result<std::string>::success(std::move(output));
}

core::Status ServerLogConfig::validate() const {
    if (rotate_after_bytes == 0 || rotate_after_bytes > max_query_file_bytes) {
        return core::Status::failure(
            "server_log.invalid_rotation_size",
            "server log rotation size must be between one byte and 512 MiB");
    }
    return core::Status::ok();
}

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
    if (metadata.size() > max_log_metadata_fields) {
        return core::Status::failure("server_log.invalid_metadata",
                                     "server log metadata has too many fields");
    }
    for (const auto& [key, value] : metadata) {
        if (!core::is_valid_local_id(key) || !valid_free_text(value)) {
            return core::Status::failure("server_log.invalid_metadata",
                                         "server log metadata key or value is invalid");
        }
    }

    std::size_t encoded_size = line_version.size() + 10U + real_timestamp_utc.size() +
                               decimal_digits(world_time) +
                               (player_uuid.has_value() ? player_uuid->value().size() : 0U);
    if (encoded_size > max_encoded_log_line_bytes ||
        !add_percent_escaped_size(encoded_size, world_calendar, max_encoded_log_line_bytes) ||
        !add_percent_escaped_size(encoded_size, event_type, max_encoded_log_line_bytes) ||
        !add_percent_escaped_size(encoded_size, server_session, max_encoded_log_line_bytes) ||
        !add_percent_escaped_size(encoded_size, display_name, max_encoded_log_line_bytes) ||
        !add_percent_escaped_size(encoded_size, message_channel, max_encoded_log_line_bytes) ||
        !add_percent_escaped_size(encoded_size, message, max_encoded_log_line_bytes)) {
        return core::Status::failure("server_log.line_too_large",
                                     "encoded server log entry exceeds one MiB");
    }
    bool first_metadata = true;
    for (const auto& [key, value] : metadata) {
        if ((!first_metadata && !add_raw_size(encoded_size, 1U, max_encoded_log_line_bytes)) ||
            !add_percent_escaped_size(encoded_size, key, max_encoded_log_line_bytes) ||
            !add_raw_size(encoded_size, 1U, max_encoded_log_line_bytes) ||
            !add_percent_escaped_size(encoded_size, value, max_encoded_log_line_bytes)) {
            return core::Status::failure("server_log.line_too_large",
                                         "encoded server log entry exceeds one MiB");
        }
        first_metadata = false;
    }
    return core::Status::ok();
}

core::Status ServerLogFilter::validate() const {
    if ((real_time_from_utc.has_value() && !valid_utc_timestamp(*real_time_from_utc)) ||
        (real_time_to_utc.has_value() && !valid_utc_timestamp(*real_time_to_utc))) {
        return core::Status::failure(
            "server_log.invalid_filter",
            "server log real-time filters must be UTC ISO-8601 timestamps with milliseconds");
    }
    if (real_time_from_utc.has_value() && real_time_to_utc.has_value() &&
        *real_time_from_utc > *real_time_to_utc) {
        return core::Status::failure("server_log.invalid_filter",
                                     "server log real-time filter range is reversed");
    }
    if (world_time_from.has_value() && world_time_to.has_value() &&
        *world_time_from > *world_time_to) {
        return core::Status::failure("server_log.invalid_filter",
                                     "server log world-time filter range is reversed");
    }
    if (player_uuid.has_value() && !player_uuid->is_valid()) {
        return core::Status::failure("server_log.invalid_filter",
                                     "server log player filter UUID is invalid");
    }
    if (!display_name.empty() && !valid_free_text(display_name)) {
        return core::Status::failure("server_log.invalid_filter",
                                     "server log display-name filter exceeds 64 KiB");
    }
    if (!event_type.empty() && !core::is_valid_local_id(event_type)) {
        return core::Status::failure("server_log.invalid_filter",
                                     "server log event filter must be a valid local id");
    }
    if (message_contains.size() > max_log_text_bytes) {
        return core::Status::failure("server_log.invalid_filter",
                                     "server log message filter exceeds 64 KiB");
    }
    if (server_session.size() > 128) {
        return core::Status::failure("server_log.invalid_filter",
                                     "server log session filter exceeds 128 bytes");
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
    if (line.size() > max_encoded_log_line_bytes || std::ranges::count(line, '|') != 10) {
        return core::Result<ServerLogEntry>::failure("server_log.invalid_line",
                                                     "server log line has an invalid shape");
    }
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

core::Status FileServerLog::initialize() {
    auto status = config_.validate();
    if (!status) {
        return status;
    }
    std::scoped_lock lock(mutex_);
    for (const auto category :
         {ServerLogCategory::general, ServerLogCategory::chat, ServerLogCategory::audit}) {
        status = ensure_category_directory(server_root_, category);
        if (!status) {
            return status;
        }
        status = ensure_appendable_file(current_path(category), "current server log");
        if (!status) {
            return status;
        }
    }
    status = ensure_rotated_directory(server_root_);
    if (!status) {
        return status;
    }
    const auto rotated_directory = server_root_ / "logs" / "rotated";
    status = ensure_appendable_file(rotated_directory / "index.log", "rotated server log index");
    if (!status) {
        return status;
    }
    auto archives = load_archive_catalog(rotated_directory, "server_log.archive_limit_reached");
    if (!archives) {
        return core::Status::failure(archives.error().code, archives.error().message);
    }
    return core::Status::ok();
}

core::Status FileServerLog::append(ServerLogCategory category, ServerLogEntry entry) {
    if (!valid_category(category)) {
        return core::Status::failure("server_log.invalid_category",
                                     "server log category is invalid");
    }
    auto status = config_.validate();
    if (!status) {
        return status;
    }
    if (entry.real_timestamp_utc.empty()) {
        entry.real_timestamp_utc = utc_now_iso8601();
    }
    status = entry.validate();
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
    status = rotate_if_needed(category, line.size(), entry.real_timestamp_utc.substr(0, 10));
    if (!status) {
        return status;
    }
    status = ensure_category_directory(server_root_, category);
    if (!status) {
        return status;
    }
    const auto path = current_path(category);
    auto current_exists = safe_regular_file_exists(path, "current server log");
    if (!current_exists) {
        return core::Status::failure(current_exists.error().code, current_exists.error().message);
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
    if (!valid_category(category)) {
        return core::Result<std::vector<ServerLogEntry>>::failure("server_log.invalid_category",
                                                                  "server log category is invalid");
    }
    auto filter_status = filter.validate();
    if (!filter_status) {
        return core::Result<std::vector<ServerLogEntry>>::failure(filter_status.error().code,
                                                                  filter_status.error().message);
    }
    std::scoped_lock lock(mutex_);
    std::vector<ServerLogEntry> result;
    auto directory_exists = category_directory_exists(server_root_, category);
    if (!directory_exists) {
        return core::Result<std::vector<ServerLogEntry>>::failure(directory_exists.error().code,
                                                                  directory_exists.error().message);
    }
    if (!directory_exists.value()) {
        return core::Result<std::vector<ServerLogEntry>>::success(std::move(result));
    }
    const auto path = current_path(category);
    auto exists = safe_regular_file_exists(path, "current server log");
    if (!exists) {
        return core::Result<std::vector<ServerLogEntry>>::failure(exists.error().code,
                                                                  exists.error().message);
    }
    if (!exists.value()) {
        return core::Result<std::vector<ServerLogEntry>>::success(std::move(result));
    }
    auto text = read_log_text(path, max_query_file_bytes, "server_log.file_too_large",
                              "current server log");
    if (!text) {
        return core::Result<std::vector<ServerLogEntry>>::failure(text.error().code,
                                                                  text.error().message);
    }
    std::size_t processed_bytes = 0;
    auto status = collect_log_text(text.value(), filter, result, processed_bytes);
    if (!status) {
        return core::Result<std::vector<ServerLogEntry>>::failure(status.error().code,
                                                                  status.error().message);
    }
    return core::Result<std::vector<ServerLogEntry>>::success(std::move(result));
}

core::Result<std::vector<ServerLogEntry>>
FileServerLog::query_all(ServerLogCategory category, const ServerLogFilter& filter) const {
    if (!valid_category(category)) {
        return core::Result<std::vector<ServerLogEntry>>::failure("server_log.invalid_category",
                                                                  "server log category is invalid");
    }
    auto filter_status = filter.validate();
    if (!filter_status) {
        return core::Result<std::vector<ServerLogEntry>>::failure(filter_status.error().code,
                                                                  filter_status.error().message);
    }
    std::scoped_lock lock(mutex_);
    std::vector<ServerLogEntry> result;
    std::size_t processed_bytes = 0;

    const auto rotated = server_root_ / "logs" / "rotated";
    auto has_rotated = rotated_directory_exists(server_root_);
    if (!has_rotated) {
        return core::Result<std::vector<ServerLogEntry>>::failure(has_rotated.error().code,
                                                                  has_rotated.error().message);
    }
    if (has_rotated.value()) {
        auto catalog = load_archive_catalog(rotated, "server_log.query_too_large");
        if (!catalog) {
            return core::Result<std::vector<ServerLogEntry>>::failure(catalog.error().code,
                                                                      catalog.error().message);
        }
        std::vector<ArchiveFile> archives;
        archives.reserve(catalog.value().size());
        for (auto& archive : catalog.value()) {
            if (archive.category == category) {
                archives.push_back(std::move(archive));
            }
        }
        std::ranges::sort(archives, [](const ArchiveFile& left, const ArchiveFile& right) {
            return std::tie(left.timestamp, left.sequence, left.path) <
                   std::tie(right.timestamp, right.sequence, right.path);
        });
        for (const auto& archive : archives) {
            auto exists = safe_regular_file_exists(archive.path, "rotated server log archive");
            if (!exists) {
                return core::Result<std::vector<ServerLogEntry>>::failure(exists.error().code,
                                                                          exists.error().message);
            }
            if (!exists.value()) {
                return core::Result<std::vector<ServerLogEntry>>::failure(
                    "server_log.read_failed",
                    "rotated server log archive disappeared during query");
            }
            const auto remaining_bytes = max_query_file_bytes - processed_bytes;
            const auto encoded_budget =
                std::min(max_query_file_bytes, archive_magic.size() + (remaining_bytes * 2U));
            auto bytes =
                read_log_bytes(archive.path, encoded_budget, "server_log.archive_too_large",
                               "rotated server log archive");
            if (!bytes) {
                return core::Result<std::vector<ServerLogEntry>>::failure(bytes.error().code,
                                                                          bytes.error().message);
            }
            auto text = ServerLogArchiveCodec::decode(bytes.value(),
                                                      {.maximum_output_bytes = remaining_bytes});
            if (!text) {
                return core::Result<std::vector<ServerLogEntry>>::failure(text.error().code,
                                                                          text.error().message);
            }
            auto status = collect_log_text(text.value(), filter, result, processed_bytes);
            if (!status) {
                return core::Result<std::vector<ServerLogEntry>>::failure(status.error().code,
                                                                          status.error().message);
            }
        }
    }

    auto has_category_directory = category_directory_exists(server_root_, category);
    if (!has_category_directory) {
        return core::Result<std::vector<ServerLogEntry>>::failure(
            has_category_directory.error().code, has_category_directory.error().message);
    }
    if (has_category_directory.value()) {
        const auto current = current_path(category);
        auto exists = safe_regular_file_exists(current, "current server log");
        if (!exists) {
            return core::Result<std::vector<ServerLogEntry>>::failure(exists.error().code,
                                                                      exists.error().message);
        }
        if (exists.value()) {
            const auto remaining_bytes = max_query_file_bytes - processed_bytes;
            auto text = read_log_text(current, remaining_bytes, "server_log.query_too_large",
                                      "current server log");
            if (!text) {
                return core::Result<std::vector<ServerLogEntry>>::failure(text.error().code,
                                                                          text.error().message);
            }
            auto status = collect_log_text(text.value(), filter, result, processed_bytes);
            if (!status) {
                return core::Result<std::vector<ServerLogEntry>>::failure(status.error().code,
                                                                          status.error().message);
            }
        }
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

core::Status FileServerLog::rotate_if_needed(ServerLogCategory category, std::size_t incoming_bytes,
                                             std::string_view incoming_day) {
    auto status = config_.validate();
    if (!status) {
        return status;
    }
    const auto current = current_path(category);
    auto has_category_directory = category_directory_exists(server_root_, category);
    if (!has_category_directory) {
        return core::Status::failure(has_category_directory.error().code,
                                     has_category_directory.error().message);
    }
    if (!has_category_directory.value()) {
        current_days_[category] = incoming_day;
        return core::Status::ok();
    }
    auto exists = safe_regular_file_exists(current, "current server log");
    if (!exists) {
        return core::Status::failure(exists.error().code, exists.error().message);
    }
    if (!exists.value()) {
        current_days_[category] = incoming_day;
        return core::Status::ok();
    }

    std::error_code error;
    const auto size = std::filesystem::file_size(current, error);
    if (error) {
        return core::Status::failure("server_log.stat_failed", error.message());
    }
    if (size > max_query_file_bytes) {
        return core::Status::failure("server_log.file_too_large",
                                     "current server log exceeds 512 MiB");
    }
    const auto modified_time = std::filesystem::last_write_time(current, error);
    if (error) {
        return core::Status::failure("server_log.stat_failed", error.message());
    }
    auto& current_day = current_days_[category];
    if (current_day.empty()) {
        auto first = read_first_log_entry(current);
        if (!first) {
            return core::Status::failure(first.error().code, first.error().message);
        }
        current_day = first.value().has_value() ? first.value()->real_timestamp_utc.substr(0, 10)
                                                : std::string(incoming_day);
    }
    if (size == 0) {
        current_day = incoming_day;
        return core::Status::ok();
    }
    const auto daily_rotation = config_.rotate_daily && current_day != incoming_day;
    if (!daily_rotation && size < config_.rotate_after_bytes &&
        incoming_bytes <= config_.rotate_after_bytes - size) {
        return core::Status::ok();
    }
    const auto archived_day = current_day;

    const auto rotated_dir = server_root_ / "logs" / "rotated";
    status = ensure_rotated_directory(server_root_);
    if (!status) {
        return status;
    }

    auto archives = load_archive_catalog(rotated_dir, "server_log.archive_limit_reached");
    if (!archives) {
        return core::Status::failure(archives.error().code, archives.error().message);
    }
    const auto category_archive_count = static_cast<std::size_t>(std::ranges::count_if(
        archives.value(), [category](const auto& item) { return item.category == category; }));
    if (category_archive_count >= max_query_archives) {
        return core::Status::failure("server_log.archive_limit_reached",
                                     "server log category reached its archive-count limit");
    }

    auto text_result = read_log_text(current, max_query_file_bytes, "server_log.file_too_large",
                                     "current server log for rotation");
    if (!text_result) {
        return core::Status::failure(text_result.error().code, text_result.error().message);
    }
    auto text = std::move(text_result).value();
    if (text.size() != size) {
        return core::Status::failure(
            "server_log.concurrent_modification",
            "current server log changed while it was being prepared for rotation");
    }
    auto archive = ServerLogArchiveCodec::encode(text);

    const auto filename_prefix = compact_timestamp(utc_now_iso8601()) + "." +
                                 std::string(server_log_category_name(category)) + ".";
    std::filesystem::path archive_path;
    std::filesystem::path temporary;
    std::string filename;
    bool found_available_path = false;
    for (std::size_t attempt = 0; attempt <= max_rotated_directory_entries; ++attempt) {
        const auto sequence = rotation_sequence_;
        rotation_sequence_ =
            sequence == std::numeric_limits<std::uint64_t>::max() ? 1U : sequence + 1U;
        filename = filename_prefix + std::to_string(sequence) + ".log.hsz";
        archive_path = rotated_dir / filename;
        temporary = archive_path.string() + ".tmp";
        auto archive_exists = safe_path_exists(archive_path, "rotated server log archive");
        if (!archive_exists) {
            return core::Status::failure(archive_exists.error().code,
                                         archive_exists.error().message);
        }
        auto temporary_exists = safe_path_exists(temporary, "temporary rotated log archive");
        if (!temporary_exists) {
            return core::Status::failure(temporary_exists.error().code,
                                         temporary_exists.error().message);
        }
        if (!archive_exists.value() && !temporary_exists.value()) {
            found_available_path = true;
            break;
        }
    }
    if (!found_available_path) {
        return core::Status::failure("server_log.archive_limit_reached",
                                     "failed to allocate a unique rotated log filename");
    }

    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            return core::Status::failure("server_log.rotate_failed",
                                         "failed to create compressed log archive");
        }
        output.write(reinterpret_cast<const char*>(archive.data()),
                     static_cast<std::streamsize>(archive.size()));
        output.close();
        if (!output) {
            std::error_code cleanup_error;
            std::filesystem::remove(temporary, cleanup_error);
            return core::Status::failure("server_log.rotate_failed",
                                         "failed to flush compressed log archive");
        }
    }
    std::filesystem::create_hard_link(temporary, archive_path, error);
    if (error) {
        std::error_code cleanup_error;
        std::filesystem::remove(temporary, cleanup_error);
        return core::Status::failure("server_log.rotate_failed", error.message());
    }
    const auto removed_temporary = std::filesystem::remove(temporary, error);
    if (error || !removed_temporary) {
        const auto temporary_error = error ? error.message() : "temporary archive disappeared";
        std::error_code cleanup_error;
        std::filesystem::remove(archive_path, cleanup_error);
        if (cleanup_error) {
            return core::Status::failure(
                "server_log.rotation_inconsistent",
                "failed to unlink temporary archive and roll back its published archive: " +
                    temporary_error + "; archive cleanup failed: " + cleanup_error.message());
        }
        return core::Status::failure("server_log.rotate_failed", temporary_error);
    }

    const auto size_after_archive = std::filesystem::file_size(current, error);
    const auto modified_time_after_archive = error
                                                 ? std::filesystem::file_time_type{}
                                                 : std::filesystem::last_write_time(current, error);
    if (error || size_after_archive != text.size() ||
        modified_time_after_archive != modified_time) {
        std::error_code cleanup_error;
        std::filesystem::remove(archive_path, cleanup_error);
        return core::Status::failure(
            "server_log.concurrent_modification",
            "current server log changed while its archive was being published");
    }
    const auto removed = std::filesystem::remove(current, error);
    if (error || !removed) {
        const auto removal_message = error ? error.message() : "current server log disappeared";
        std::error_code cleanup_error;
        std::filesystem::remove(archive_path, cleanup_error);
        if (cleanup_error) {
            return core::Status::failure(
                "server_log.rotation_inconsistent",
                "failed to remove current log and its duplicate archive: " + removal_message +
                    "; archive cleanup failed: " + cleanup_error.message());
        }
        return core::Status::failure("server_log.rotate_failed", removal_message);
    }
    current_day = incoming_day;
    return write_rotation_index(rotated_dir, filename, category, archived_day, incoming_day,
                                text.size(), archive.size());
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
