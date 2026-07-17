#include "engine/player_profiles/player_profile.hpp"

#include "engine/core/filesystem.hpp"
#include "engine/core/ids.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <fstream>
#include <limits>
#include <set>
#include <span>
#include <sstream>
#include <system_error>
#include <utility>

namespace heartstead::player_profiles {

namespace {

constexpr std::string_view magic = "heartstead.player_profile.v1";
constexpr std::size_t max_display_name_bytes = 128;
constexpr std::size_t max_character_data_bytes = 1024U * 1024U;
constexpr std::uintmax_t max_profile_file_bytes = 4U * 1024U * 1024U;
constexpr std::uintmax_t max_map_discovery_file_bytes = 256U * 1024U * 1024U;
constexpr std::size_t max_display_name_history = 64;
constexpr std::size_t max_roles = 64;
constexpr std::size_t max_markers = 4096;
constexpr std::size_t max_flags = 4096;
constexpr std::size_t max_world_settings = 1024;

[[nodiscard]] bool is_hex(char value) noexcept {
    return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f') ||
           (value >= 'A' && value <= 'F');
}

[[nodiscard]] bool valid_display_name(std::string_view value) noexcept {
    if (value.empty() || value.size() > max_display_name_bytes) {
        return false;
    }
    return std::ranges::none_of(
        value, [](unsigned char character) { return character < 0x20U || character == 0x7FU; });
}

[[nodiscard]] std::string percent_escape(std::string_view input) {
    std::string result;
    constexpr char hex[] = "0123456789ABCDEF";
    for (const auto character : input) {
        const auto byte = static_cast<unsigned char>(character);
        if (character == '%' || character == '|' || character == '=' || character == '\n' ||
            character == '\r') {
            result.push_back('%');
            result.push_back(hex[(byte >> 4U) & 0x0FU]);
            result.push_back(hex[byte & 0x0FU]);
        } else {
            result.push_back(character);
        }
    }
    return result;
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

[[nodiscard]] core::Result<std::string> percent_unescape(std::string_view input) {
    std::string result;
    result.reserve(input.size());
    for (std::size_t index = 0; index < input.size(); ++index) {
        if (input[index] != '%') {
            result.push_back(input[index]);
            continue;
        }
        if (index + 2 >= input.size() || !is_hex(input[index + 1]) || !is_hex(input[index + 2])) {
            return core::Result<std::string>::failure("player_profile.invalid_escape",
                                                      "player profile contains an invalid escape");
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

template <typename T>
[[nodiscard]] core::Result<T> parse_integer(std::string_view value, std::string_view label) {
    T parsed{};
    const auto [ptr, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (error != std::errc{} || ptr != value.data() + value.size()) {
        return core::Result<T>::failure("player_profile.invalid_number",
                                        "invalid player profile number: " + std::string(label));
    }
    return core::Result<T>::success(parsed);
}

[[nodiscard]] std::string encode_coord(world::BlockCoord coord) {
    return std::to_string(coord.x) + "|" + std::to_string(coord.y) + "|" + std::to_string(coord.z);
}

[[nodiscard]] core::Result<world::BlockCoord> decode_coord(std::span<const std::string_view> parts,
                                                           std::string_view label) {
    if (parts.size() != 3) {
        return core::Result<world::BlockCoord>::failure("player_profile.invalid_coord",
                                                        "player profile coordinate is malformed");
    }
    auto x = parse_integer<std::int64_t>(parts[0], std::string(label) + "_x");
    auto y = parse_integer<std::int64_t>(parts[1], std::string(label) + "_y");
    auto z = parse_integer<std::int64_t>(parts[2], std::string(label) + "_z");
    if (!x || !y || !z) {
        return core::Result<world::BlockCoord>::failure("player_profile.invalid_coord",
                                                        "player profile coordinate is malformed");
    }
    return core::Result<world::BlockCoord>::success({x.value(), y.value(), z.value()});
}

[[nodiscard]] std::string encode_marker(const PlayerMapMarker& marker) {
    return percent_escape(marker.id) + "|" + percent_escape(marker.label) + "|" +
           percent_escape(marker.layer_id) + "|" + encode_coord(marker.position) + "|" +
           (marker.private_to_player ? "1" : "0");
}

[[nodiscard]] core::Result<PlayerMapMarker> decode_marker(std::string_view value) {
    const auto parts = split(value, '|');
    if (parts.size() != 7) {
        return core::Result<PlayerMapMarker>::failure("player_profile.invalid_marker",
                                                      "player map marker is malformed");
    }
    auto id = percent_unescape(parts[0]);
    auto label = percent_unescape(parts[1]);
    auto layer = percent_unescape(parts[2]);
    const std::array<std::string_view, 3> coord_parts{parts[3], parts[4], parts[5]};
    auto coord = decode_coord(coord_parts, "marker");
    if (!id || !label || !layer || !coord || (parts[6] != "0" && parts[6] != "1")) {
        return core::Result<PlayerMapMarker>::failure("player_profile.invalid_marker",
                                                      "player map marker is malformed");
    }
    PlayerMapMarker marker{std::move(id).value(), std::move(label).value(),
                           std::move(layer).value(), coord.value(), parts[6] == "1"};
    auto status = marker.validate();
    if (!status) {
        return core::Result<PlayerMapMarker>::failure(status.error().code, status.error().message);
    }
    return core::Result<PlayerMapMarker>::success(std::move(marker));
}

[[nodiscard]] core::Status validate_token_vector(const std::vector<std::string>& values,
                                                 std::string_view label) {
    std::set<std::string> seen;
    for (const auto& value : values) {
        if (!core::is_valid_local_id(value)) {
            return core::Status::failure("player_profile.invalid_token",
                                         std::string(label) + " contains an invalid id: " + value);
        }
        if (!seen.insert(value).second) {
            return core::Status::failure("player_profile.duplicate_token",
                                         std::string(label) + " contains a duplicate id: " + value);
        }
    }
    return core::Status::ok();
}

[[nodiscard]] core::Status filesystem_error(std::string code, const std::error_code& error) {
    return core::Status::failure(std::move(code), error.message());
}

[[nodiscard]] core::Status write_atomic(const std::filesystem::path& path, std::string_view text) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        return filesystem_error("player_profile.create_directory_failed", error);
    }
    auto temporary = path;
    temporary += ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            return core::Status::failure("player_profile.write_failed",
                                         "failed to open temporary player profile file");
        }
        output.write(text.data(), static_cast<std::streamsize>(text.size()));
        output.flush();
        if (!output) {
            return core::Status::failure("player_profile.write_failed",
                                         "failed to write temporary player profile file");
        }
    }
    error = core::replace_file(temporary, path);
    if (error) {
        const auto rename_error = error;
        std::error_code cleanup_error;
        std::filesystem::remove(temporary, cleanup_error);
        return filesystem_error("player_profile.rename_failed", rename_error);
    }
    return core::Status::ok();
}

[[nodiscard]] core::Result<std::string> read_text(const std::filesystem::path& path,
                                                  std::uintmax_t max_bytes) {
    std::error_code error;
    const auto file_bytes = std::filesystem::file_size(path, error);
    if (error) {
        return core::Result<std::string>::failure("player_profile.read_failed", error.message());
    }
    if (file_bytes > max_bytes) {
        return core::Result<std::string>::failure("player_profile.file_too_large",
                                                  "player profile data exceeds its size limit");
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return core::Result<std::string>::failure("player_profile.read_failed",
                                                  "failed to open player profile file");
    }
    std::ostringstream output;
    output << input.rdbuf();
    if (!input.good() && !input.eof()) {
        return core::Result<std::string>::failure("player_profile.read_failed",
                                                  "failed to read player profile file");
    }
    auto text = output.str();
    if (text.size() > max_bytes) {
        return core::Result<std::string>::failure("player_profile.file_too_large",
                                                  "player profile data exceeds its size limit");
    }
    return core::Result<std::string>::success(std::move(text));
}

} // namespace

std::optional<PlayerUuid> PlayerUuid::parse(std::string_view value) {
    if (value.size() != 36 || value[8] != '-' || value[13] != '-' || value[18] != '-' ||
        value[23] != '-') {
        return std::nullopt;
    }
    std::string normalized;
    normalized.reserve(value.size());
    bool has_nonzero = false;
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (index == 8 || index == 13 || index == 18 || index == 23) {
            normalized.push_back('-');
            continue;
        }
        if (!is_hex(value[index])) {
            return std::nullopt;
        }
        const auto lower =
            static_cast<char>(std::tolower(static_cast<unsigned char>(value[index])));
        has_nonzero = has_nonzero || lower != '0';
        normalized.push_back(lower);
    }
    if (!has_nonzero) {
        return std::nullopt;
    }
    return PlayerUuid(std::move(normalized));
}

bool PlayerUuid::is_valid() const noexcept {
    return !value_.empty();
}

const std::string& PlayerUuid::value() const noexcept {
    return value_;
}

core::Status PlayerMapMarker::validate() const {
    if (!core::is_valid_local_id(id)) {
        return core::Status::failure("player_profile.invalid_marker_id",
                                     "player map marker id must be a valid local id");
    }
    if (label.empty() || label.size() > 256) {
        return core::Status::failure("player_profile.invalid_marker_label",
                                     "player map marker label must be 1..256 bytes");
    }
    if (!core::is_valid_local_id(layer_id)) {
        return core::Status::failure("player_profile.invalid_marker_layer",
                                     "player map marker layer must be a valid local id");
    }
    return core::Status::ok();
}

core::Status PlayerProfile::validate() const {
    if (!player_uuid.is_valid()) {
        return core::Status::failure("player_profile.invalid_uuid",
                                     "player profile requires a stable UUID");
    }
    if (revision == 0) {
        return core::Status::failure("player_profile.invalid_revision",
                                     "player profile revision must be non-zero");
    }
    if (display_names_history.empty()) {
        return core::Status::failure("player_profile.missing_display_name",
                                     "player profile requires a display name history");
    }
    if (display_names_history.size() > max_display_name_history || roles.size() > max_roles ||
        waypoints.size() > max_markers || personal_markers.size() > max_markers ||
        handbook_flags.size() > max_flags || progression_flags.size() > max_flags ||
        world_settings.size() > max_world_settings) {
        return core::Status::failure("player_profile.collection_too_large",
                                     "player profile collection exceeds its safety limit");
    }
    for (const auto& name : display_names_history) {
        if (!valid_display_name(name)) {
            return core::Status::failure(
                "player_profile.invalid_display_name",
                "player display name is empty, too long, or contains controls");
        }
    }
    auto status = validate_token_vector(roles, "roles");
    if (!status) {
        return status;
    }
    status = validate_token_vector(handbook_flags, "handbook flags");
    if (!status) {
        return status;
    }
    status = validate_token_vector(progression_flags, "progression flags");
    if (!status) {
        return status;
    }
    std::set<std::string> marker_ids;
    for (const auto& marker : waypoints) {
        status = marker.validate();
        if (!status) {
            return status;
        }
        if (!marker_ids.insert(marker.id).second) {
            return core::Status::failure("player_profile.duplicate_marker",
                                         "player marker id is duplicated: " + marker.id);
        }
    }
    for (const auto& marker : personal_markers) {
        status = marker.validate();
        if (!status) {
            return status;
        }
        if (!marker_ids.insert(marker.id).second) {
            return core::Status::failure("player_profile.duplicate_marker",
                                         "player marker id is duplicated: " + marker.id);
        }
    }
    for (const auto& [key, value] : world_settings) {
        if (!core::is_valid_local_id(key) || value.size() > 4096) {
            return core::Status::failure(
                "player_profile.invalid_setting",
                "player world setting has an invalid key or oversized value");
        }
    }
    if (character_data.size() > max_character_data_bytes) {
        return core::Status::failure("player_profile.character_data_too_large",
                                     "player character data exceeds one MiB");
    }
    for (const auto* region : map_discovery.regions()) {
        status = region->validate();
        if (!status) {
            return status;
        }
    }
    return core::Status::ok();
}

std::string_view PlayerProfile::current_display_name() const noexcept {
    return display_names_history.empty() ? std::string_view{} : display_names_history.back();
}

core::Status PlayerProfile::remember_display_name(std::string name) {
    if (!valid_display_name(name)) {
        return core::Status::failure(
            "player_profile.invalid_display_name",
            "player display name is empty, too long, or contains controls");
    }
    if (!display_names_history.empty() && display_names_history.back() == name) {
        return core::Status::ok();
    }
    if (revision == std::numeric_limits<std::uint64_t>::max()) {
        return core::Status::failure("player_profile.revision_overflow",
                                     "player profile revision cannot overflow u64");
    }
    if (display_names_history.size() >= max_display_name_history) {
        display_names_history.erase(display_names_history.begin());
    }
    display_names_history.push_back(std::move(name));
    ++revision;
    return core::Status::ok();
}

std::string PlayerProfileTextCodec::encode(const PlayerProfile& profile) {
    std::ostringstream output;
    output << magic << '\n';
    output << "uuid=" << profile.player_uuid.value() << '\n';
    output << "revision=" << profile.revision << '\n';
    for (const auto& name : profile.display_names_history) {
        output << "name=" << percent_escape(name) << '\n';
    }
    for (const auto& role : profile.roles) {
        output << "role=" << role << '\n';
    }
    if (profile.spawn.has_value()) {
        output << "spawn=" << encode_coord(*profile.spawn) << '\n';
    }
    if (profile.bed.has_value()) {
        output << "bed=" << encode_coord(*profile.bed) << '\n';
    }
    for (const auto& marker : profile.waypoints) {
        output << "waypoint=" << encode_marker(marker) << '\n';
    }
    for (const auto& marker : profile.personal_markers) {
        output << "marker=" << encode_marker(marker) << '\n';
    }
    for (const auto& flag : profile.handbook_flags) {
        output << "handbook=" << flag << '\n';
    }
    for (const auto& flag : profile.progression_flags) {
        output << "progression=" << flag << '\n';
    }
    for (const auto& [key, value] : profile.world_settings) {
        output << "setting=" << key << '|' << percent_escape(value) << '\n';
    }
    if (!profile.character_data.empty()) {
        output << "character=" << percent_escape(profile.character_data) << '\n';
    }
    output << "end\n";
    return output.str();
}

core::Result<PlayerProfile> PlayerProfileTextCodec::decode(std::string_view text) {
    if (text.size() > max_profile_file_bytes) {
        return core::Result<PlayerProfile>::failure("player_profile.file_too_large",
                                                    "player profile exceeds its size limit");
    }
    PlayerProfile profile;
    bool saw_magic = false;
    bool saw_end = false;
    bool saw_uuid = false;
    bool saw_revision = false;
    std::size_t consumed_bytes = 0;

    std::size_t line_start = 0;
    while (line_start <= text.size()) {
        const auto line_end = text.find('\n', line_start);
        auto line = line_end == std::string_view::npos
                        ? text.substr(line_start)
                        : text.substr(line_start, line_end - line_start);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        if (!saw_magic) {
            if (line != magic) {
                return core::Result<PlayerProfile>::failure(
                    "player_profile.invalid_magic", "player profile file has an invalid header");
            }
            saw_magic = true;
        } else if (line == "end") {
            saw_end = true;
            consumed_bytes = line_end == std::string_view::npos ? text.size() : line_end + 1;
            break;
        } else if (!line.empty()) {
            const auto separator = line.find('=');
            if (separator == std::string_view::npos) {
                return core::Result<PlayerProfile>::failure("player_profile.invalid_line",
                                                            "player profile record lacks '='");
            }
            const auto key = line.substr(0, separator);
            const auto value = line.substr(separator + 1);
            if (key == "uuid") {
                if (saw_uuid) {
                    return core::Result<PlayerProfile>::failure(
                        "player_profile.duplicate_uuid", "player profile UUID is duplicated");
                }
                auto uuid = PlayerUuid::parse(value);
                if (!uuid) {
                    return core::Result<PlayerProfile>::failure("player_profile.invalid_uuid",
                                                                "player profile UUID is invalid");
                }
                profile.player_uuid = *uuid;
                saw_uuid = true;
            } else if (key == "revision") {
                if (saw_revision) {
                    return core::Result<PlayerProfile>::failure(
                        "player_profile.duplicate_revision",
                        "player profile revision is duplicated");
                }
                auto revision = parse_integer<std::uint64_t>(value, "revision");
                if (!revision) {
                    return core::Result<PlayerProfile>::failure(revision.error().code,
                                                                revision.error().message);
                }
                profile.revision = revision.value();
                saw_revision = true;
            } else if (key == "name") {
                auto parsed = percent_unescape(value);
                if (!parsed) {
                    return core::Result<PlayerProfile>::failure(parsed.error().code,
                                                                parsed.error().message);
                }
                profile.display_names_history.push_back(std::move(parsed).value());
            } else if (key == "role") {
                profile.roles.emplace_back(value);
            } else if (key == "spawn" || key == "bed") {
                const auto parts = split(value, '|');
                auto coord = decode_coord(parts, key);
                if (!coord) {
                    return core::Result<PlayerProfile>::failure(coord.error().code,
                                                                coord.error().message);
                }
                if (key == "spawn") {
                    if (profile.spawn.has_value()) {
                        return core::Result<PlayerProfile>::failure(
                            "player_profile.duplicate_spawn", "player profile spawn is duplicated");
                    }
                    profile.spawn = coord.value();
                } else {
                    if (profile.bed.has_value()) {
                        return core::Result<PlayerProfile>::failure(
                            "player_profile.duplicate_bed", "player profile bed is duplicated");
                    }
                    profile.bed = coord.value();
                }
            } else if (key == "waypoint" || key == "marker") {
                auto marker = decode_marker(value);
                if (!marker) {
                    return core::Result<PlayerProfile>::failure(marker.error().code,
                                                                marker.error().message);
                }
                (key == "waypoint" ? profile.waypoints : profile.personal_markers)
                    .push_back(std::move(marker).value());
            } else if (key == "handbook") {
                profile.handbook_flags.emplace_back(value);
            } else if (key == "progression") {
                profile.progression_flags.emplace_back(value);
            } else if (key == "setting") {
                const auto parts = split(value, '|');
                if (parts.size() != 2) {
                    return core::Result<PlayerProfile>::failure(
                        "player_profile.invalid_setting", "player profile setting is malformed");
                }
                auto setting_value = percent_unescape(parts[1]);
                if (!setting_value ||
                    !profile.world_settings
                         .emplace(std::string(parts[0]), std::move(setting_value).value())
                         .second) {
                    return core::Result<PlayerProfile>::failure(
                        "player_profile.invalid_setting",
                        "player profile setting is invalid or duplicated");
                }
            } else if (key == "character") {
                if (!profile.character_data.empty()) {
                    return core::Result<PlayerProfile>::failure(
                        "player_profile.duplicate_character",
                        "player character data is duplicated");
                }
                auto character = percent_unescape(value);
                if (!character) {
                    return core::Result<PlayerProfile>::failure(character.error().code,
                                                                character.error().message);
                }
                profile.character_data = std::move(character).value();
            } else {
                return core::Result<PlayerProfile>::failure("player_profile.unknown_key",
                                                            "unknown player profile record: " +
                                                                std::string(key));
            }
        }
        if (line_end == std::string_view::npos) {
            break;
        }
        line_start = line_end + 1;
    }

    if (!saw_magic || !saw_end || !saw_uuid || !saw_revision) {
        return core::Result<PlayerProfile>::failure(
            "player_profile.incomplete", "player profile file is missing required records");
    }
    if (consumed_bytes != text.size()) {
        return core::Result<PlayerProfile>::failure(
            "player_profile.trailing_data", "player profile file contains data after end marker");
    }
    auto status = profile.validate();
    if (!status) {
        return core::Result<PlayerProfile>::failure(status.error().code, status.error().message);
    }
    return core::Result<PlayerProfile>::success(std::move(profile));
}

FilePlayerProfileStore::FilePlayerProfileStore(std::filesystem::path world_root)
    : world_root_(std::move(world_root)) {}

const std::filesystem::path& FilePlayerProfileStore::world_root() const noexcept {
    return world_root_;
}

std::filesystem::path FilePlayerProfileStore::profile_directory(const PlayerUuid& uuid) const {
    return world_root_ / "player_profiles" / uuid.value();
}

core::Status FilePlayerProfileStore::save(const PlayerProfile& profile) const {
    std::scoped_lock lock(mutex_);
    return save_unlocked(profile);
}

core::Status FilePlayerProfileStore::save_unlocked(const PlayerProfile& profile) const {
    auto status = profile.validate();
    if (!status) {
        return status;
    }
    const auto directory = profile_directory(profile.player_uuid);
    auto staging = directory;
    staging += ".tmp";
    auto previous = directory;
    previous += ".old";
    std::error_code error;
    std::filesystem::remove_all(staging, error);
    if (error)
        return filesystem_error("player_profile.cleanup_failed", error);
    status = write_atomic(staging / "profile.dat", PlayerProfileTextCodec::encode(profile));
    if (!status) {
        std::filesystem::remove_all(staging, error);
        return status;
    }
    status = write_atomic(staging / "map_discovery.dat",
                          MapDiscoveryTextCodec::encode(profile.map_discovery));
    if (!status) {
        std::filesystem::remove_all(staging, error);
        return status;
    }

    std::filesystem::remove_all(previous, error);
    if (error)
        return filesystem_error("player_profile.cleanup_failed", error);
    const auto had_previous = std::filesystem::exists(directory, error);
    if (error)
        return filesystem_error("player_profile.commit_failed", error);
    if (had_previous) {
        std::filesystem::rename(directory, previous, error);
        if (error)
            return filesystem_error("player_profile.commit_failed", error);
    }
    std::filesystem::rename(staging, directory, error);
    if (error) {
        const auto commit_error = error;
        if (had_previous) {
            error.clear();
            std::filesystem::rename(previous, directory, error);
        }
        std::filesystem::remove_all(staging, error);
        return filesystem_error("player_profile.commit_failed", commit_error);
    }
    std::filesystem::remove_all(previous, error);
    return core::Status::ok();
}

core::Result<PlayerProfile> FilePlayerProfileStore::load(const PlayerUuid& uuid) const {
    if (!uuid.is_valid()) {
        return core::Result<PlayerProfile>::failure("player_profile.invalid_uuid",
                                                    "player profile UUID is invalid");
    }
    std::scoped_lock lock(mutex_);
    return load_unlocked(uuid);
}

core::Result<PlayerProfile> FilePlayerProfileStore::load_unlocked(const PlayerUuid& uuid) const {
    const auto directory = profile_directory(uuid);
    auto previous = directory;
    previous += ".old";
    std::error_code recovery_error;
    if (!std::filesystem::exists(directory, recovery_error) && !recovery_error &&
        std::filesystem::exists(previous, recovery_error) && !recovery_error) {
        std::filesystem::rename(previous, directory, recovery_error);
    }
    if (recovery_error)
        return core::Result<PlayerProfile>::failure("player_profile.recovery_failed",
                                                    recovery_error.message());
    auto text = read_text(directory / "profile.dat", max_profile_file_bytes);
    if (!text) {
        return core::Result<PlayerProfile>::failure(text.error().code, text.error().message);
    }
    auto profile = PlayerProfileTextCodec::decode(text.value());
    if (!profile) {
        return profile;
    }
    if (profile.value().player_uuid != uuid) {
        return core::Result<PlayerProfile>::failure(
            "player_profile.uuid_mismatch", "stored player UUID does not match its directory");
    }

    std::error_code error;
    const auto map_path = directory / "map_discovery.dat";
    const auto has_map = std::filesystem::exists(map_path, error);
    if (error) {
        return core::Result<PlayerProfile>::failure("player_profile.read_failed", error.message());
    }
    if (!has_map) {
        return core::Result<PlayerProfile>::failure(
            "player_profile.missing_map_discovery",
            "player profile is incomplete because map_discovery.dat is missing");
    }
    auto map_text = read_text(map_path, max_map_discovery_file_bytes);
    if (!map_text) {
        return core::Result<PlayerProfile>::failure(map_text.error().code,
                                                    map_text.error().message);
    }
    auto discovery = MapDiscoveryTextCodec::decode(map_text.value());
    if (!discovery) {
        return core::Result<PlayerProfile>::failure(discovery.error().code,
                                                    discovery.error().message);
    }
    profile.value().map_discovery = std::move(discovery).value();
    return profile;
}

core::Result<PlayerProfile>
FilePlayerProfileStore::load_or_create(const PlayerUuid& uuid,
                                       std::string initial_display_name) const {
    if (!uuid.is_valid() || !valid_display_name(initial_display_name)) {
        return core::Result<PlayerProfile>::failure(
            "player_profile.invalid_create_request",
            "player profile creation requires a valid UUID and display name");
    }
    std::scoped_lock lock(mutex_);
    const auto path = profile_directory(uuid) / "profile.dat";
    std::error_code error;
    const auto exists = std::filesystem::exists(path, error);
    if (error) {
        return core::Result<PlayerProfile>::failure("player_profile.read_failed", error.message());
    }
    if (exists) {
        return load_unlocked(uuid);
    }

    PlayerProfile profile;
    profile.player_uuid = uuid;
    profile.display_names_history.push_back(std::move(initial_display_name));
    profile.roles.push_back("player");
    auto status = save_unlocked(profile);
    if (!status) {
        return core::Result<PlayerProfile>::failure(status.error().code, status.error().message);
    }
    return core::Result<PlayerProfile>::success(std::move(profile));
}

core::Result<std::vector<PlayerUuid>> FilePlayerProfileStore::list_profiles() const {
    std::scoped_lock lock(mutex_);
    std::vector<PlayerUuid> result;
    const auto root = world_root_ / "player_profiles";
    std::error_code error;
    const auto exists = std::filesystem::exists(root, error);
    if (error) {
        return core::Result<std::vector<PlayerUuid>>::failure("player_profile.list_failed",
                                                              error.message());
    }
    if (!exists) {
        return core::Result<std::vector<PlayerUuid>>::success(std::move(result));
    }
    for (const auto& entry : std::filesystem::directory_iterator(root, error)) {
        if (error) {
            return core::Result<std::vector<PlayerUuid>>::failure("player_profile.list_failed",
                                                                  error.message());
        }
        if (!entry.is_directory(error)) {
            error.clear();
            continue;
        }
        auto uuid = PlayerUuid::parse(entry.path().filename().string());
        if (uuid) {
            result.push_back(*uuid);
        }
    }
    std::ranges::sort(result);
    return core::Result<std::vector<PlayerUuid>>::success(std::move(result));
}

} // namespace heartstead::player_profiles
