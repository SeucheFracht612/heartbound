#include "engine/save/save_slot.hpp"

#include "engine/core/ids.hpp"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <sstream>
#include <system_error>
#include <utility>

namespace heartstead::save {

namespace {

constexpr std::string_view slot_metadata_magic = "heartstead.save_slot.v1";

[[nodiscard]] std::filesystem::path slot_path(const std::filesystem::path& root,
                                              std::string_view slot_id) {
    return root / std::string(slot_id);
}

[[nodiscard]] std::filesystem::path slot_metadata_path(const std::filesystem::path& root,
                                                       std::string_view slot_id) {
    return slot_path(root, slot_id) / "slot.txt";
}

[[nodiscard]] core::Status filesystem_failure(std::string code, const std::error_code& error) {
    return core::Status::failure(std::move(code), error.message());
}

[[nodiscard]] core::Status validate_slot_id(std::string_view slot_id) {
    if (!FileSaveSlotCatalog::is_valid_slot_id(slot_id)) {
        return core::Status::failure("save_slot.invalid_id",
                                     "save slot id must be a safe lowercase directory name");
    }
    return core::Status::ok();
}

[[nodiscard]] bool is_safe_display_name(std::string_view display_name) noexcept {
    if (display_name.empty() || display_name.size() > 96) {
        return false;
    }
    return std::ranges::none_of(display_name, [](char value) {
        const auto character = static_cast<unsigned char>(value);
        return character < 0x20 || character > 0x7e;
    });
}

[[nodiscard]] SaveSlotMetadata default_metadata(std::string_view slot_id) {
    const auto id = std::string(slot_id);
    return SaveSlotMetadata{.slot_id = id, .display_name = id};
}

[[nodiscard]] core::Status ensure_parent_directory(const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        return filesystem_failure("save_slot.create_failed", error);
    }
    return core::Status::ok();
}

[[nodiscard]] core::Status write_text_atomic(const std::filesystem::path& path,
                                             std::string_view text) {
    auto status = ensure_parent_directory(path);
    if (!status) {
        return status;
    }

    const auto temporary = path.string() + ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            return core::Status::failure("save_slot.write_failed",
                                         "failed to open save slot file for writing: " + temporary);
        }
        output << text;
        if (!output) {
            return core::Status::failure("save_slot.write_failed",
                                         "failed to write save slot file: " + temporary);
        }
    }

    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::filesystem::remove(path, error);
        error.clear();
        std::filesystem::rename(temporary, path, error);
    }
    if (error) {
        return filesystem_failure("save_slot.rename_failed", error);
    }
    return core::Status::ok();
}

[[nodiscard]] core::Result<std::string> read_text(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return core::Result<std::string>::failure(
            "save_slot.read_failed", "failed to open save slot file: " + path.string());
    }

    std::ostringstream output;
    output << input.rdbuf();
    if (input.bad()) {
        return core::Result<std::string>::failure(
            "save_slot.read_failed", "failed to read save slot file: " + path.string());
    }
    return core::Result<std::string>::success(output.str());
}

[[nodiscard]] core::Result<std::uint64_t> parse_u64(std::string_view value,
                                                    std::string_view field_name) {
    if (value.empty()) {
        return core::Result<std::uint64_t>::failure("save_slot.invalid_metadata",
                                                    "empty save slot metadata field: " +
                                                        std::string(field_name));
    }

    std::uint64_t parsed = 0;
    const auto* begin = value.data();
    const auto* end = value.data() + value.size();
    const auto [ptr, error] = std::from_chars(begin, end, parsed);
    if (error != std::errc{} || ptr != end) {
        return core::Result<std::uint64_t>::failure("save_slot.invalid_metadata",
                                                    "invalid save slot metadata field: " +
                                                        std::string(field_name));
    }
    return core::Result<std::uint64_t>::success(parsed);
}

[[nodiscard]] std::string encode_metadata(const SaveSlotMetadata& metadata) {
    std::ostringstream output;
    output << slot_metadata_magic << '\n';
    output << "slot_id|" << metadata.slot_id << '\n';
    output << "display_name|" << metadata.display_name << '\n';
    output << "created_at_ms|" << metadata.created_at_ms << '\n';
    output << "last_saved_at_ms|" << metadata.last_saved_at_ms << '\n';
    output << "end\n";
    return output.str();
}

[[nodiscard]] core::Status duplicate_field_failure(std::string_view field_name) {
    return core::Status::failure("save_slot.invalid_metadata",
                                 "duplicate save slot metadata field: " + std::string(field_name));
}

[[nodiscard]] core::Result<SaveSlotMetadata> decode_metadata(std::string_view text) {
    std::istringstream input{std::string(text)};
    std::string line;
    if (!std::getline(input, line) || line != slot_metadata_magic) {
        return core::Result<SaveSlotMetadata>::failure("save_slot.invalid_metadata",
                                                       "save slot metadata has an invalid header");
    }

    SaveSlotMetadata metadata;
    auto saw_slot_id = false;
    auto saw_display_name = false;
    auto saw_created_at_ms = false;
    auto saw_last_saved_at_ms = false;
    auto saw_end = false;

    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line == "end") {
            saw_end = true;
            break;
        }

        const auto separator = line.find('|');
        if (separator == std::string::npos) {
            return core::Result<SaveSlotMetadata>::failure(
                "save_slot.invalid_metadata", "save slot metadata line is missing a separator");
        }
        const auto key = std::string_view(line).substr(0, separator);
        const auto value = std::string_view(line).substr(separator + 1);

        if (key == "slot_id") {
            if (saw_slot_id) {
                const auto status = duplicate_field_failure(key);
                return core::Result<SaveSlotMetadata>::failure(status.error().code,
                                                               status.error().message);
            }
            metadata.slot_id = std::string(value);
            saw_slot_id = true;
        } else if (key == "display_name") {
            if (saw_display_name) {
                const auto status = duplicate_field_failure(key);
                return core::Result<SaveSlotMetadata>::failure(status.error().code,
                                                               status.error().message);
            }
            metadata.display_name = std::string(value);
            saw_display_name = true;
        } else if (key == "created_at_ms") {
            if (saw_created_at_ms) {
                const auto status = duplicate_field_failure(key);
                return core::Result<SaveSlotMetadata>::failure(status.error().code,
                                                               status.error().message);
            }
            auto parsed = parse_u64(value, key);
            if (!parsed) {
                return core::Result<SaveSlotMetadata>::failure(parsed.error().code,
                                                               parsed.error().message);
            }
            metadata.created_at_ms = parsed.value();
            saw_created_at_ms = true;
        } else if (key == "last_saved_at_ms") {
            if (saw_last_saved_at_ms) {
                const auto status = duplicate_field_failure(key);
                return core::Result<SaveSlotMetadata>::failure(status.error().code,
                                                               status.error().message);
            }
            auto parsed = parse_u64(value, key);
            if (!parsed) {
                return core::Result<SaveSlotMetadata>::failure(parsed.error().code,
                                                               parsed.error().message);
            }
            metadata.last_saved_at_ms = parsed.value();
            saw_last_saved_at_ms = true;
        }
    }

    if (!saw_end || !saw_slot_id || !saw_display_name || !saw_created_at_ms ||
        !saw_last_saved_at_ms) {
        return core::Result<SaveSlotMetadata>::failure("save_slot.incomplete_metadata",
                                                       "save slot metadata is incomplete");
    }

    const auto status = metadata.validate();
    if (!status) {
        return core::Result<SaveSlotMetadata>::failure(status.error().code, status.error().message);
    }
    return core::Result<SaveSlotMetadata>::success(std::move(metadata));
}

} // namespace

core::Status SaveSlotMetadata::validate() const {
    auto status = validate_slot_id(slot_id);
    if (!status) {
        return status;
    }
    if (!is_safe_display_name(display_name)) {
        return core::Status::failure(
            "save_slot.invalid_display_name",
            "save slot display name must be 1-96 printable ASCII characters");
    }
    if (created_at_ms != 0 && last_saved_at_ms != 0 && last_saved_at_ms < created_at_ms) {
        return core::Status::failure(
            "save_slot.invalid_timestamps",
            "save slot last-saved timestamp must not be older than its created timestamp");
    }
    return core::Status::ok();
}

FileSaveSlotCatalog::FileSaveSlotCatalog(std::filesystem::path root) : root_(std::move(root)) {}

const std::filesystem::path& FileSaveSlotCatalog::root() const noexcept {
    return root_;
}

bool FileSaveSlotCatalog::is_valid_slot_id(std::string_view slot_id) noexcept {
    return core::is_valid_namespace_id(slot_id);
}

core::Status FileSaveSlotCatalog::create_slot(std::string_view slot_id) const {
    auto status = validate_slot_id(slot_id);
    if (!status) {
        return status;
    }

    std::error_code error;
    std::filesystem::create_directories(slot_path(root_, slot_id), error);
    if (error) {
        return filesystem_failure("save_slot.create_failed", error);
    }
    const auto metadata_path = slot_metadata_path(root_, slot_id);
    const auto has_metadata = std::filesystem::exists(metadata_path, error);
    if (error) {
        return filesystem_failure("save_slot.read_failed", error);
    }
    if (!has_metadata) {
        return write_metadata(default_metadata(slot_id));
    }
    return core::Status::ok();
}

core::Result<FileSaveDatabase> FileSaveSlotCatalog::database(std::string_view slot_id) const {
    auto status = validate_slot_id(slot_id);
    if (!status) {
        return core::Result<FileSaveDatabase>::failure(status.error().code, status.error().message);
    }
    return core::Result<FileSaveDatabase>::success(FileSaveDatabase(slot_path(root_, slot_id)));
}

core::Status FileSaveSlotCatalog::write_snapshot(std::string_view slot_id,
                                                 const SaveSnapshot& snapshot,
                                                 std::uint64_t saved_at_ms) const {
    if (saved_at_ms == 0) {
        return core::Status::failure("save_slot.invalid_timestamps",
                                     "save slot snapshot commits require a nonzero timestamp");
    }

    auto status = create_slot(slot_id);
    if (!status) {
        return status;
    }

    auto metadata = read_metadata(slot_id);
    if (!metadata) {
        return core::Status::failure(metadata.error().code, metadata.error().message);
    }
    if (metadata.value().created_at_ms == 0) {
        metadata.value().created_at_ms = saved_at_ms;
    }
    metadata.value().last_saved_at_ms = saved_at_ms;

    status = metadata.value().validate();
    if (!status) {
        return status;
    }

    FileSaveDatabase slot_database(slot_path(root_, slot_id));
    status = slot_database.write_snapshot(snapshot);
    if (!status) {
        return status;
    }
    return write_metadata(metadata.value());
}

core::Status FileSaveSlotCatalog::write_metadata(const SaveSlotMetadata& metadata) const {
    auto status = metadata.validate();
    if (!status) {
        return status;
    }
    return write_text_atomic(slot_metadata_path(root_, metadata.slot_id),
                             encode_metadata(metadata));
}

core::Result<SaveSlotMetadata> FileSaveSlotCatalog::read_metadata(std::string_view slot_id) const {
    auto status = validate_slot_id(slot_id);
    if (!status) {
        return core::Result<SaveSlotMetadata>::failure(status.error().code, status.error().message);
    }

    std::error_code error;
    const auto path = slot_metadata_path(root_, slot_id);
    const auto has_metadata = std::filesystem::exists(path, error);
    if (error) {
        return core::Result<SaveSlotMetadata>::failure("save_slot.read_failed", error.message());
    }
    if (!has_metadata) {
        return core::Result<SaveSlotMetadata>::success(default_metadata(slot_id));
    }

    auto text = read_text(path);
    if (!text) {
        return core::Result<SaveSlotMetadata>::failure(text.error().code, text.error().message);
    }
    auto metadata = decode_metadata(text.value());
    if (!metadata) {
        return metadata;
    }
    if (metadata.value().slot_id != slot_id) {
        return core::Result<SaveSlotMetadata>::failure(
            "save_slot.metadata_mismatch",
            "save slot metadata id does not match its directory name");
    }
    return metadata;
}

core::Result<std::vector<SaveSlotSummary>> FileSaveSlotCatalog::list_slots() const {
    std::vector<SaveSlotSummary> summaries;

    std::error_code error;
    const bool exists = std::filesystem::exists(root_, error);
    if (error) {
        return core::Result<std::vector<SaveSlotSummary>>::failure("save_slot.list_failed",
                                                                   error.message());
    }
    if (!exists) {
        return core::Result<std::vector<SaveSlotSummary>>::success(std::move(summaries));
    }

    for (const auto& entry : std::filesystem::directory_iterator(root_, error)) {
        if (error) {
            return core::Result<std::vector<SaveSlotSummary>>::failure("save_slot.list_failed",
                                                                       error.message());
        }
        if (!entry.is_directory(error)) {
            error.clear();
            continue;
        }

        const auto slot_id = entry.path().filename().string();
        if (!is_valid_slot_id(slot_id)) {
            continue;
        }

        auto metadata = read_metadata(slot_id);
        if (!metadata) {
            return core::Result<std::vector<SaveSlotSummary>>::failure(metadata.error().code,
                                                                       metadata.error().message);
        }
        FileSaveDatabase database(entry.path());
        auto stats = database.stats();
        if (!stats) {
            return core::Result<std::vector<SaveSlotSummary>>::failure(stats.error().code,
                                                                       stats.error().message);
        }
        summaries.push_back(
            {slot_id, entry.path(), std::move(metadata).value(), std::move(stats).value()});
    }
    if (error) {
        return core::Result<std::vector<SaveSlotSummary>>::failure("save_slot.list_failed",
                                                                   error.message());
    }

    std::ranges::sort(summaries, [](const SaveSlotSummary& left, const SaveSlotSummary& right) {
        return left.slot_id < right.slot_id;
    });
    return core::Result<std::vector<SaveSlotSummary>>::success(std::move(summaries));
}

core::Result<SaveSlotCatalogSummary> FileSaveSlotCatalog::summary() const {
    auto slots = list_slots();
    if (!slots) {
        return core::Result<SaveSlotCatalogSummary>::failure(slots.error().code,
                                                             slots.error().message);
    }
    return core::Result<SaveSlotCatalogSummary>::success(
        SaveSlotCatalogSummary{.root = root_, .slots = std::move(slots).value()});
}

} // namespace heartstead::save
