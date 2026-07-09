#include "engine/save/save_database.hpp"

#include "engine/save/save_binary_codec.hpp"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>

namespace heartstead::save {

namespace {

constexpr std::string_view chunk_index_magic = "heartstead.save_database_chunks.v1";
constexpr std::string_view current_generation_magic = "heartstead.save_database_current.v1";
constexpr std::string_view generation_prefix = "generation_";

struct ChunkIndexEntry {
    world::ChunkCoord coord;
    std::string filename;
};

struct GenerationDirectoryStats {
    std::size_t committed_count = 0;
    std::size_t staged_count = 0;
};

struct CommittedGenerationEntry {
    std::uint64_t number = 0;
    std::string name;
    std::filesystem::path path;
};

struct StagedGenerationEntry {
    std::uint64_t number = 0;
    std::filesystem::path path;
};

[[nodiscard]] std::filesystem::path snapshot_path(const std::filesystem::path& root) {
    return root / "snapshot.hssb";
}

[[nodiscard]] std::filesystem::path generations_directory(const std::filesystem::path& root) {
    return root / "generations";
}

[[nodiscard]] std::filesystem::path current_generation_path(const std::filesystem::path& root) {
    return root / "current.txt";
}

[[nodiscard]] std::filesystem::path chunk_directory(const std::filesystem::path& root) {
    return root / "chunks";
}

[[nodiscard]] std::filesystem::path chunk_index_path(const std::filesystem::path& root) {
    return chunk_directory(root) / "index.txt";
}

[[nodiscard]] std::string chunk_filename(world::ChunkCoord coord) {
    return "c_" + std::to_string(coord.x) + "_" + std::to_string(coord.y) + "_" +
           std::to_string(coord.z) + ".delta";
}

[[nodiscard]] bool same_coord(world::ChunkCoord left, world::ChunkCoord right) noexcept {
    return left.x == right.x && left.y == right.y && left.z == right.z;
}

[[nodiscard]] bool is_generation_name(std::string_view name) noexcept {
    if (!name.starts_with(generation_prefix) || name.size() == generation_prefix.size()) {
        return false;
    }
    for (const auto character : name.substr(generation_prefix.size())) {
        if (character < '0' || character > '9') {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool is_staged_generation_name(std::string_view name) noexcept {
    constexpr std::string_view staged_suffix = ".tmp";
    return name.ends_with(staged_suffix) &&
           is_generation_name(name.substr(0, name.size() - staged_suffix.size()));
}

[[nodiscard]] core::Result<std::uint64_t> parse_generation_number(std::string_view name) {
    if (!is_generation_name(name)) {
        return core::Result<std::uint64_t>::failure("save_database.invalid_generation_name",
                                                    "save generation name is invalid");
    }

    std::uint64_t parsed = 0;
    const auto digits = name.substr(generation_prefix.size());
    const auto* begin = digits.data();
    const auto* end = digits.data() + digits.size();
    const auto [ptr, error] = std::from_chars(begin, end, parsed);
    if (error != std::errc{} || ptr != end || parsed == 0) {
        return core::Result<std::uint64_t>::failure("save_database.invalid_generation_name",
                                                    "save generation number is invalid");
    }
    return core::Result<std::uint64_t>::success(parsed);
}

[[nodiscard]] std::string generation_name(std::uint64_t generation) {
    return std::string(generation_prefix) + std::to_string(generation);
}

[[nodiscard]] core::Status filesystem_failure(std::string code, const std::error_code& error) {
    return core::Status::failure(std::move(code), error.message());
}

[[nodiscard]] core::Status ensure_parent_directory(const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        return filesystem_failure("save_database.create_directory_failed", error);
    }
    return core::Status::ok();
}

[[nodiscard]] core::Status remove_tree(const std::filesystem::path& path, std::string code) {
    std::error_code error;
    std::filesystem::remove_all(path, error);
    if (error) {
        return filesystem_failure(std::move(code), error);
    }
    return core::Status::ok();
}

[[nodiscard]] core::Status rename_path(const std::filesystem::path& from,
                                       const std::filesystem::path& to, std::string code) {
    std::error_code error;
    std::filesystem::rename(from, to, error);
    if (error) {
        return filesystem_failure(std::move(code), error);
    }
    return core::Status::ok();
}

[[nodiscard]] core::Status write_bytes_atomic(const std::filesystem::path& path,
                                              std::span<const std::uint8_t> bytes) {
    auto status = ensure_parent_directory(path);
    if (!status) {
        return status;
    }

    const auto temporary = path.string() + ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            return core::Status::failure("save_database.write_failed",
                                         "failed to open save database file for writing: " +
                                             temporary);
        }
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        if (!output) {
            return core::Status::failure("save_database.write_failed",
                                         "failed to write save database file: " + temporary);
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
        return filesystem_failure("save_database.rename_failed", error);
    }
    return core::Status::ok();
}

[[nodiscard]] core::Status write_text_atomic(const std::filesystem::path& path,
                                             std::string_view text) {
    return write_bytes_atomic(
        path, std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(text.data()),
                                            text.size()));
}

[[nodiscard]] core::Result<std::vector<std::uint8_t>>
read_bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return core::Result<std::vector<std::uint8_t>>::failure(
            "save_database.read_failed", "failed to open save database file: " + path.string());
    }

    input.seekg(0, std::ios::end);
    const auto end = input.tellg();
    if (end < 0) {
        return core::Result<std::vector<std::uint8_t>>::failure(
            "save_database.read_failed", "failed to determine save database file size");
    }
    input.seekg(0, std::ios::beg);

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
    }
    if (!input) {
        return core::Result<std::vector<std::uint8_t>>::failure(
            "save_database.read_failed", "failed to read save database file: " + path.string());
    }
    return core::Result<std::vector<std::uint8_t>>::success(std::move(bytes));
}

[[nodiscard]] core::Result<std::int64_t> parse_i64(std::string_view value,
                                                   std::string_view field_name) {
    std::int64_t parsed = 0;
    const auto* begin = value.data();
    const auto* end = value.data() + value.size();
    const auto [ptr, error] = std::from_chars(begin, end, parsed);
    if (error != std::errc{} || ptr != end) {
        return core::Result<std::int64_t>::failure("save_database.invalid_chunk_index",
                                                   "invalid chunk index field: " +
                                                       std::string(field_name));
    }
    return core::Result<std::int64_t>::success(parsed);
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

[[nodiscard]] core::Result<std::vector<ChunkIndexEntry>>
read_chunk_index(const std::filesystem::path& root) {
    const auto path = chunk_index_path(root);
    if (!std::filesystem::exists(path)) {
        return core::Result<std::vector<ChunkIndexEntry>>::success({});
    }

    auto bytes = read_bytes(path);
    if (!bytes) {
        return core::Result<std::vector<ChunkIndexEntry>>::failure(bytes.error().code,
                                                                   bytes.error().message);
    }
    const auto text =
        std::string_view(reinterpret_cast<const char*>(bytes.value().data()), bytes.value().size());
    std::vector<ChunkIndexEntry> entries;
    bool saw_magic = false;
    bool saw_end = false;

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
            if (line != chunk_index_magic) {
                return core::Result<std::vector<ChunkIndexEntry>>::failure(
                    "save_database.invalid_chunk_index", "chunk index has invalid magic");
            }
            saw_magic = true;
        } else if (line == "end") {
            saw_end = true;
            break;
        } else if (!line.empty()) {
            const auto fields = split(line, '|');
            if (fields.size() != 5 || fields.front() != "chunk") {
                return core::Result<std::vector<ChunkIndexEntry>>::failure(
                    "save_database.invalid_chunk_index",
                    "chunk index row must be chunk|x|y|z|filename");
            }
            auto x = parse_i64(fields[1], "x");
            auto y = parse_i64(fields[2], "y");
            auto z = parse_i64(fields[3], "z");
            if (!x || !y || !z || fields[4].empty() ||
                fields[4].find('/') != std::string_view::npos ||
                fields[4].find('\\') != std::string_view::npos) {
                return core::Result<std::vector<ChunkIndexEntry>>::failure(
                    "save_database.invalid_chunk_index", "chunk index row contains invalid fields");
            }
            entries.push_back({{x.value(), y.value(), z.value()}, std::string(fields[4])});
        }

        if (line_end == std::string_view::npos) {
            break;
        }
        line_start = line_end + 1;
    }

    if (!saw_magic || !saw_end) {
        return core::Result<std::vector<ChunkIndexEntry>>::failure(
            "save_database.incomplete_chunk_index", "chunk index is incomplete");
    }
    return core::Result<std::vector<ChunkIndexEntry>>::success(std::move(entries));
}

[[nodiscard]] core::Status write_chunk_index(const std::filesystem::path& root,
                                             const std::vector<ChunkIndexEntry>& entries) {
    std::ostringstream output;
    output << chunk_index_magic << '\n';
    for (const auto& entry : entries) {
        output << "chunk|" << entry.coord.x << '|' << entry.coord.y << '|' << entry.coord.z << '|'
               << entry.filename << '\n';
    }
    output << "end\n";
    return write_text_atomic(chunk_index_path(root), output.str());
}

[[nodiscard]] core::Status write_current_generation(const std::filesystem::path& root,
                                                    std::string_view name) {
    if (!is_generation_name(name)) {
        return core::Status::failure("save_database.invalid_generation_name",
                                     "save generation name is invalid");
    }

    std::ostringstream output;
    output << current_generation_magic << '\n';
    output << "active|" << name << '\n';
    output << "end\n";
    return write_text_atomic(current_generation_path(root), output.str());
}

[[nodiscard]] core::Result<std::string> read_current_generation(const std::filesystem::path& root) {
    auto bytes = read_bytes(current_generation_path(root));
    if (!bytes) {
        return core::Result<std::string>::failure(bytes.error().code, bytes.error().message);
    }

    const auto text =
        std::string_view(reinterpret_cast<const char*>(bytes.value().data()), bytes.value().size());
    bool saw_magic = false;
    bool saw_end = false;
    std::string active_generation;

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
            if (line != current_generation_magic) {
                return core::Result<std::string>::failure(
                    "save_database.invalid_generation_manifest",
                    "save generation manifest has invalid magic");
            }
            saw_magic = true;
        } else if (line == "end") {
            saw_end = true;
            break;
        } else if (!line.empty()) {
            const auto fields = split(line, '|');
            if (fields.size() != 2 || fields.front() != "active") {
                return core::Result<std::string>::failure(
                    "save_database.invalid_generation_manifest",
                    "save generation manifest must contain active|generation_number");
            }
            auto generation_number = parse_generation_number(fields[1]);
            if (!generation_number) {
                return core::Result<std::string>::failure(
                    "save_database.invalid_generation_manifest",
                    "save generation manifest contains an invalid generation name");
            }
            if (!active_generation.empty()) {
                return core::Result<std::string>::failure(
                    "save_database.invalid_generation_manifest",
                    "save generation manifest declares more than one active generation");
            }
            active_generation = std::string(fields[1]);
        }

        if (line_end == std::string_view::npos) {
            break;
        }
        line_start = line_end + 1;
    }

    if (!saw_magic || !saw_end || active_generation.empty()) {
        return core::Result<std::string>::failure("save_database.incomplete_generation_manifest",
                                                  "save generation manifest is incomplete");
    }
    return core::Result<std::string>::success(std::move(active_generation));
}

[[nodiscard]] core::Result<std::filesystem::path>
active_save_root(const std::filesystem::path& root) {
    std::error_code error;
    const auto manifest = current_generation_path(root);
    const bool has_manifest = std::filesystem::exists(manifest, error);
    if (error) {
        return core::Result<std::filesystem::path>::failure("save_database.read_failed",
                                                            error.message());
    }
    if (!has_manifest) {
        return core::Result<std::filesystem::path>::success(root);
    }

    auto active = read_current_generation(root);
    if (!active) {
        return core::Result<std::filesystem::path>::failure(active.error().code,
                                                            active.error().message);
    }

    auto generation_root = generations_directory(root) / active.value();
    const bool exists = std::filesystem::is_directory(generation_root, error);
    if (error) {
        return core::Result<std::filesystem::path>::failure("save_database.read_failed",
                                                            error.message());
    }
    if (!exists) {
        return core::Result<std::filesystem::path>::failure(
            "save_database.missing_generation", "active save generation directory is missing");
    }

    return core::Result<std::filesystem::path>::success(std::move(generation_root));
}

[[nodiscard]] core::Result<std::string> next_generation_name(const std::filesystem::path& root) {
    std::uint64_t highest_generation = 0;
    const auto generations = generations_directory(root);

    std::error_code error;
    const bool has_generations = std::filesystem::exists(generations, error);
    if (error) {
        return core::Result<std::string>::failure("save_database.read_failed", error.message());
    }
    if (!has_generations) {
        return core::Result<std::string>::success(generation_name(1));
    }

    for (const auto& entry : std::filesystem::directory_iterator(generations, error)) {
        if (error) {
            return core::Result<std::string>::failure("save_database.read_failed", error.message());
        }
        if (!entry.is_directory(error)) {
            error.clear();
            continue;
        }
        const auto name = entry.path().filename().string();
        if (!is_generation_name(name)) {
            continue;
        }
        auto parsed = parse_generation_number(name);
        if (!parsed) {
            return core::Result<std::string>::failure(parsed.error().code, parsed.error().message);
        }
        highest_generation = std::max(highest_generation, parsed.value());
    }
    if (error) {
        return core::Result<std::string>::failure("save_database.read_failed", error.message());
    }

    return core::Result<std::string>::success(generation_name(highest_generation + 1));
}

[[nodiscard]] core::Result<GenerationDirectoryStats>
collect_generation_directory_stats(const std::filesystem::path& root) {
    GenerationDirectoryStats stats;
    const auto generations = generations_directory(root);

    std::error_code error;
    const bool has_generations = std::filesystem::exists(generations, error);
    if (error) {
        return core::Result<GenerationDirectoryStats>::failure("save_database.stats_failed",
                                                               error.message());
    }
    if (!has_generations) {
        return core::Result<GenerationDirectoryStats>::success(stats);
    }

    for (const auto& entry : std::filesystem::directory_iterator(generations, error)) {
        if (error) {
            return core::Result<GenerationDirectoryStats>::failure("save_database.stats_failed",
                                                                   error.message());
        }
        if (!entry.is_directory(error)) {
            error.clear();
            continue;
        }

        const auto name = entry.path().filename().string();
        if (is_generation_name(name)) {
            ++stats.committed_count;
        } else if (is_staged_generation_name(name)) {
            ++stats.staged_count;
        }
    }
    if (error) {
        return core::Result<GenerationDirectoryStats>::failure("save_database.stats_failed",
                                                               error.message());
    }

    return core::Result<GenerationDirectoryStats>::success(stats);
}

[[nodiscard]] core::Result<std::vector<CommittedGenerationEntry>>
collect_committed_generations(const std::filesystem::path& root) {
    std::vector<CommittedGenerationEntry> result;
    const auto generations = generations_directory(root);

    std::error_code error;
    const bool has_generations = std::filesystem::exists(generations, error);
    if (error) {
        return core::Result<std::vector<CommittedGenerationEntry>>::failure(
            "save_database.prune_failed", error.message());
    }
    if (!has_generations) {
        return core::Result<std::vector<CommittedGenerationEntry>>::success(std::move(result));
    }

    for (const auto& entry : std::filesystem::directory_iterator(generations, error)) {
        if (error) {
            return core::Result<std::vector<CommittedGenerationEntry>>::failure(
                "save_database.prune_failed", error.message());
        }
        if (!entry.is_directory(error)) {
            error.clear();
            continue;
        }

        const auto name = entry.path().filename().string();
        if (!is_generation_name(name)) {
            continue;
        }
        auto parsed = parse_generation_number(name);
        if (!parsed) {
            return core::Result<std::vector<CommittedGenerationEntry>>::failure(
                parsed.error().code, parsed.error().message);
        }
        result.push_back({parsed.value(), name, entry.path()});
    }
    if (error) {
        return core::Result<std::vector<CommittedGenerationEntry>>::failure(
            "save_database.prune_failed", error.message());
    }

    return core::Result<std::vector<CommittedGenerationEntry>>::success(std::move(result));
}

[[nodiscard]] core::Result<std::vector<StagedGenerationEntry>>
collect_staged_generations(const std::filesystem::path& root) {
    std::vector<StagedGenerationEntry> result;
    const auto generations = generations_directory(root);

    std::error_code error;
    const bool has_generations = std::filesystem::exists(generations, error);
    if (error) {
        return core::Result<std::vector<StagedGenerationEntry>>::failure(
            "save_database.recover_failed", error.message());
    }
    if (!has_generations) {
        return core::Result<std::vector<StagedGenerationEntry>>::success(std::move(result));
    }

    constexpr std::string_view staged_suffix = ".tmp";
    for (const auto& entry : std::filesystem::directory_iterator(generations, error)) {
        if (error) {
            return core::Result<std::vector<StagedGenerationEntry>>::failure(
                "save_database.recover_failed", error.message());
        }
        if (!entry.is_directory(error)) {
            error.clear();
            continue;
        }

        const auto name = entry.path().filename().string();
        if (!is_staged_generation_name(name)) {
            continue;
        }
        auto parsed = parse_generation_number(
            std::string_view(name).substr(0, name.size() - staged_suffix.size()));
        if (!parsed) {
            return core::Result<std::vector<StagedGenerationEntry>>::failure(
                parsed.error().code, parsed.error().message);
        }
        result.push_back({parsed.value(), entry.path()});
    }
    if (error) {
        return core::Result<std::vector<StagedGenerationEntry>>::failure(
            "save_database.recover_failed", error.message());
    }

    return core::Result<std::vector<StagedGenerationEntry>>::success(std::move(result));
}

[[nodiscard]] core::Result<std::string>
read_chunk_delta_payload(const std::filesystem::path& path) {
    auto bytes = read_bytes(path);
    if (!bytes) {
        return core::Result<std::string>::failure(bytes.error().code, bytes.error().message);
    }
    return core::Result<std::string>::success(
        std::string(reinterpret_cast<const char*>(bytes.value().data()), bytes.value().size()));
}

[[nodiscard]] core::Status write_chunk_delta_payload(const std::filesystem::path& path,
                                                     std::string_view payload) {
    return write_bytes_atomic(
        path, std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(payload.data()),
                                            payload.size()));
}

[[nodiscard]] core::Result<std::vector<ChunkEditSaveRecord>>
read_chunk_deltas_from_root(const std::filesystem::path& save_root) {
    auto entries = read_chunk_index(save_root);
    if (!entries) {
        return core::Result<std::vector<ChunkEditSaveRecord>>::failure(entries.error().code,
                                                                       entries.error().message);
    }

    std::vector<ChunkEditSaveRecord> result;
    result.reserve(entries.value().size());
    for (const auto& entry : entries.value()) {
        auto payload = read_chunk_delta_payload(chunk_directory(save_root) / entry.filename);
        if (!payload) {
            return core::Result<std::vector<ChunkEditSaveRecord>>::failure(payload.error().code,
                                                                           payload.error().message);
        }
        result.push_back({entry.coord, std::move(payload).value()});
    }
    return core::Result<std::vector<ChunkEditSaveRecord>>::success(std::move(result));
}

[[nodiscard]] core::Status
write_chunk_deltas_to_root(const std::filesystem::path& save_root,
                           std::span<const ChunkEditSaveRecord> chunk_deltas) {
    std::vector<ChunkIndexEntry> entries;
    entries.reserve(chunk_deltas.size());

    auto status =
        remove_tree(chunk_directory(save_root), "save_database.remove_chunk_table_failed");
    if (!status) {
        return status;
    }

    for (const auto& chunk_delta : chunk_deltas) {
        if (chunk_delta.encoded_edit_delta.empty()) {
            return core::Status::failure("save_database.empty_chunk_delta",
                                         "chunk delta payload must not be empty");
        }
        const auto filename = chunk_filename(chunk_delta.coord);
        status = write_chunk_delta_payload(chunk_directory(save_root) / filename,
                                           chunk_delta.encoded_edit_delta);
        if (!status) {
            return status;
        }

        const auto found =
            std::ranges::find_if(entries, [&chunk_delta](const ChunkIndexEntry& entry) {
                return same_coord(entry.coord, chunk_delta.coord);
            });
        if (found == entries.end()) {
            entries.push_back({chunk_delta.coord, filename});
        } else {
            found->filename = filename;
        }
    }

    std::ranges::sort(entries, [](const ChunkIndexEntry& left, const ChunkIndexEntry& right) {
        if (left.coord.x != right.coord.x) {
            return left.coord.x < right.coord.x;
        }
        if (left.coord.y != right.coord.y) {
            return left.coord.y < right.coord.y;
        }
        return left.coord.z < right.coord.z;
    });

    return write_chunk_index(save_root, entries);
}

} // namespace

FileSaveDatabase::FileSaveDatabase(std::filesystem::path root) : root_(std::move(root)) {}

bool SaveDatabaseMaintenanceResult::changed() const noexcept {
    return recovered_staged_generation_count > 0 || pruned_stale_generation_count > 0 ||
           compacted_chunk_delta_count > 0;
}

bool SaveDatabaseMigrationResult::changed() const noexcept {
    return wrote_snapshot || !migration.applied_migrations.empty();
}

const std::filesystem::path& FileSaveDatabase::root() const noexcept {
    return root_;
}

core::Status FileSaveDatabase::write_snapshot(const SaveSnapshot& snapshot) const {
    auto generation = next_generation_name(root_);
    if (!generation) {
        return core::Status::failure(generation.error().code, generation.error().message);
    }

    const auto staged_root = generations_directory(root_) / (generation.value() + ".tmp");
    const auto committed_root = generations_directory(root_) / generation.value();

    auto status = remove_tree(staged_root, "save_database.remove_staged_generation_failed");
    if (!status) {
        return status;
    }

    const auto encoded = SaveBinaryCodec::encode_snapshot(snapshot);
    status = write_bytes_atomic(snapshot_path(staged_root), encoded);
    if (!status) {
        (void)remove_tree(staged_root, "save_database.remove_staged_generation_failed");
        return status;
    }

    status = write_chunk_deltas_to_root(staged_root, snapshot.chunk_edits);
    if (!status) {
        (void)remove_tree(staged_root, "save_database.remove_staged_generation_failed");
        return status;
    }

    status = rename_path(staged_root, committed_root, "save_database.commit_generation_failed");
    if (!status) {
        (void)remove_tree(staged_root, "save_database.remove_staged_generation_failed");
        return status;
    }

    return write_current_generation(root_, generation.value());
}

core::Result<SaveSnapshot> FileSaveDatabase::read_snapshot() const {
    auto save_root = active_save_root(root_);
    if (!save_root) {
        return core::Result<SaveSnapshot>::failure(save_root.error().code,
                                                   save_root.error().message);
    }

    auto bytes = read_bytes(snapshot_path(save_root.value()));
    if (!bytes) {
        return core::Result<SaveSnapshot>::failure(bytes.error().code, bytes.error().message);
    }

    auto snapshot = SaveBinaryCodec::decode_snapshot(bytes.value());
    if (!snapshot) {
        return core::Result<SaveSnapshot>::failure(snapshot.error().code, snapshot.error().message);
    }

    auto chunk_deltas = read_chunk_deltas_from_root(save_root.value());
    if (!chunk_deltas) {
        return core::Result<SaveSnapshot>::failure(chunk_deltas.error().code,
                                                   chunk_deltas.error().message);
    }
    if (!chunk_deltas.value().empty()) {
        snapshot.value().chunk_edits = std::move(chunk_deltas).value();
    }

    return snapshot;
}

core::Result<SaveSnapshot>
FileSaveDatabase::read_validated_snapshot(const modding::PrototypeRegistry& prototypes) const {
    auto snapshot = read_snapshot();
    if (!snapshot) {
        return core::Result<SaveSnapshot>::failure(snapshot.error().code, snapshot.error().message);
    }

    const auto validation = SaveSnapshotValidator::validate(snapshot.value(), prototypes);
    if (!validation.valid()) {
        const auto& first_issue = validation.issues.front();
        return core::Result<SaveSnapshot>::failure(first_issue.code, first_issue.message);
    }

    return snapshot;
}

core::Status FileSaveDatabase::write_chunk_delta(const ChunkEditSaveRecord& chunk_delta) const {
    if (chunk_delta.encoded_edit_delta.empty()) {
        return core::Status::failure("save_database.empty_chunk_delta",
                                     "chunk delta payload must not be empty");
    }

    auto save_root = active_save_root(root_);
    if (!save_root) {
        return core::Status::failure(save_root.error().code, save_root.error().message);
    }

    auto entries = read_chunk_index(save_root.value());
    if (!entries) {
        return core::Status::failure(entries.error().code, entries.error().message);
    }

    const auto filename = chunk_filename(chunk_delta.coord);
    const auto found =
        std::ranges::find_if(entries.value(), [&chunk_delta](const ChunkIndexEntry& entry) {
            return same_coord(entry.coord, chunk_delta.coord);
        });
    if (found == entries.value().end()) {
        entries.value().push_back({chunk_delta.coord, filename});
    } else {
        found->filename = filename;
    }

    auto status = write_chunk_delta_payload(chunk_directory(save_root.value()) / filename,
                                            chunk_delta.encoded_edit_delta);
    if (!status) {
        return status;
    }
    return write_chunk_index(save_root.value(), entries.value());
}

core::Status
FileSaveDatabase::write_chunk_deltas(std::span<const ChunkEditSaveRecord> chunk_deltas) const {
    auto save_root = active_save_root(root_);
    if (!save_root) {
        return core::Status::failure(save_root.error().code, save_root.error().message);
    }
    return write_chunk_deltas_to_root(save_root.value(), chunk_deltas);
}

core::Result<ChunkEditSaveRecord>
FileSaveDatabase::read_chunk_delta(world::ChunkCoord coord) const {
    auto save_root = active_save_root(root_);
    if (!save_root) {
        return core::Result<ChunkEditSaveRecord>::failure(save_root.error().code,
                                                          save_root.error().message);
    }

    auto entries = read_chunk_index(save_root.value());
    if (!entries) {
        return core::Result<ChunkEditSaveRecord>::failure(entries.error().code,
                                                          entries.error().message);
    }

    const auto found = std::ranges::find_if(entries.value(), [coord](const ChunkIndexEntry& entry) {
        return same_coord(entry.coord, coord);
    });
    if (found == entries.value().end()) {
        return core::Result<ChunkEditSaveRecord>::failure(
            "save_database.missing_chunk_delta", "chunk delta is not present in save database");
    }

    auto payload = read_chunk_delta_payload(chunk_directory(save_root.value()) / found->filename);
    if (!payload) {
        return core::Result<ChunkEditSaveRecord>::failure(payload.error().code,
                                                          payload.error().message);
    }
    return core::Result<ChunkEditSaveRecord>::success({found->coord, std::move(payload).value()});
}

core::Result<std::vector<ChunkEditSaveRecord>> FileSaveDatabase::read_chunk_deltas() const {
    auto save_root = active_save_root(root_);
    if (!save_root) {
        return core::Result<std::vector<ChunkEditSaveRecord>>::failure(save_root.error().code,
                                                                       save_root.error().message);
    }
    return read_chunk_deltas_from_root(save_root.value());
}

core::Result<std::size_t> FileSaveDatabase::compact_chunk_deltas() const {
    auto save_root = active_save_root(root_);
    if (!save_root) {
        return core::Result<std::size_t>::failure(save_root.error().code,
                                                  save_root.error().message);
    }

    auto entries = read_chunk_index(save_root.value());
    if (!entries) {
        return core::Result<std::size_t>::failure(entries.error().code, entries.error().message);
    }

    std::unordered_set<std::string> referenced_files;
    referenced_files.reserve(entries.value().size());
    for (const auto& entry : entries.value()) {
        referenced_files.insert(entry.filename);
    }

    const auto chunks = chunk_directory(save_root.value());
    std::error_code error;
    const bool has_chunks = std::filesystem::exists(chunks, error);
    if (error) {
        return core::Result<std::size_t>::failure("save_database.compact_failed", error.message());
    }
    if (!has_chunks) {
        return core::Result<std::size_t>::success(0);
    }

    std::size_t removed_count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(chunks, error)) {
        if (error) {
            return core::Result<std::size_t>::failure("save_database.compact_failed",
                                                      error.message());
        }
        if (!entry.is_regular_file(error)) {
            error.clear();
            continue;
        }

        const auto filename = entry.path().filename().string();
        if (entry.path().extension() != ".delta" || referenced_files.contains(filename)) {
            continue;
        }

        std::filesystem::remove(entry.path(), error);
        if (error) {
            return core::Result<std::size_t>::failure("save_database.compact_failed",
                                                      error.message());
        }
        ++removed_count;
    }
    if (error) {
        return core::Result<std::size_t>::failure("save_database.compact_failed", error.message());
    }

    return core::Result<std::size_t>::success(removed_count);
}

core::Status FileSaveDatabase::prune_stale_generations(std::size_t keep_stale_generations) const {
    std::error_code error;
    const bool has_manifest = std::filesystem::exists(current_generation_path(root_), error);
    if (error) {
        return filesystem_failure("save_database.prune_failed", error);
    }
    if (!has_manifest) {
        return core::Status::ok();
    }

    auto active = read_current_generation(root_);
    if (!active) {
        return core::Status::failure(active.error().code, active.error().message);
    }

    auto active_root = active_save_root(root_);
    if (!active_root) {
        return core::Status::failure(active_root.error().code, active_root.error().message);
    }

    auto committed_generations = collect_committed_generations(root_);
    if (!committed_generations) {
        return core::Status::failure(committed_generations.error().code,
                                     committed_generations.error().message);
    }

    std::vector<CommittedGenerationEntry> stale_generations;
    stale_generations.reserve(committed_generations.value().size());
    for (const auto& generation : committed_generations.value()) {
        if (generation.name != active.value()) {
            stale_generations.push_back(generation);
        }
    }

    std::ranges::sort(stale_generations, [](const CommittedGenerationEntry& left,
                                            const CommittedGenerationEntry& right) {
        return left.number > right.number;
    });

    for (std::size_t index = keep_stale_generations; index < stale_generations.size(); ++index) {
        auto status = remove_tree(stale_generations[index].path, "save_database.prune_failed");
        if (!status) {
            return status;
        }
    }

    return core::Status::ok();
}

core::Result<std::size_t> FileSaveDatabase::recover_staged_generations() const {
    std::error_code error;
    const bool has_manifest = std::filesystem::exists(current_generation_path(root_), error);
    if (error) {
        return core::Result<std::size_t>::failure("save_database.recover_failed", error.message());
    }
    if (has_manifest) {
        auto active_root = active_save_root(root_);
        if (!active_root) {
            return core::Result<std::size_t>::failure(active_root.error().code,
                                                      active_root.error().message);
        }
    }

    auto staged_generations = collect_staged_generations(root_);
    if (!staged_generations) {
        return core::Result<std::size_t>::failure(staged_generations.error().code,
                                                  staged_generations.error().message);
    }

    std::ranges::sort(staged_generations.value(),
                      [](const StagedGenerationEntry& left, const StagedGenerationEntry& right) {
                          return left.number < right.number;
                      });

    std::size_t removed_count = 0;
    for (const auto& staged_generation : staged_generations.value()) {
        auto status = remove_tree(staged_generation.path, "save_database.recover_failed");
        if (!status) {
            return core::Result<std::size_t>::failure(status.error().code, status.error().message);
        }
        ++removed_count;
    }

    return core::Result<std::size_t>::success(removed_count);
}

core::Result<SaveDatabaseMaintenanceResult>
FileSaveDatabase::maintain(const SaveDatabaseMaintenancePolicy& policy) const {
    auto before = stats();
    if (!before) {
        return core::Result<SaveDatabaseMaintenanceResult>::failure(before.error().code,
                                                                    before.error().message);
    }

    SaveDatabaseMaintenanceResult result;
    result.before = before.value();

    if (policy.recover_staged_generations) {
        auto recovered = recover_staged_generations();
        if (!recovered) {
            return core::Result<SaveDatabaseMaintenanceResult>::failure(recovered.error().code,
                                                                        recovered.error().message);
        }
        result.recovered_staged_generation_count = recovered.value();
    }

    if (policy.prune_stale_generations) {
        auto pre_prune = stats();
        if (!pre_prune) {
            return core::Result<SaveDatabaseMaintenanceResult>::failure(pre_prune.error().code,
                                                                        pre_prune.error().message);
        }
        auto status = prune_stale_generations(policy.keep_stale_generations);
        if (!status) {
            return core::Result<SaveDatabaseMaintenanceResult>::failure(status.error().code,
                                                                        status.error().message);
        }
        auto post_prune = stats();
        if (!post_prune) {
            return core::Result<SaveDatabaseMaintenanceResult>::failure(post_prune.error().code,
                                                                        post_prune.error().message);
        }
        if (pre_prune.value().committed_generation_count >
            post_prune.value().committed_generation_count) {
            result.pruned_stale_generation_count = pre_prune.value().committed_generation_count -
                                                   post_prune.value().committed_generation_count;
        }
    }

    if (policy.compact_chunk_deltas) {
        auto compacted = compact_chunk_deltas();
        if (!compacted) {
            return core::Result<SaveDatabaseMaintenanceResult>::failure(compacted.error().code,
                                                                        compacted.error().message);
        }
        result.compacted_chunk_delta_count = compacted.value();
    }

    auto after = stats();
    if (!after) {
        return core::Result<SaveDatabaseMaintenanceResult>::failure(after.error().code,
                                                                    after.error().message);
    }
    result.after = after.value();
    return core::Result<SaveDatabaseMaintenanceResult>::success(std::move(result));
}

core::Result<SaveDatabaseMigrationResult>
FileSaveDatabase::migrate_to_schema(const SaveMigrationRegistry& registry,
                                    std::uint32_t target_schema_version) const {
    auto before = stats();
    if (!before) {
        return core::Result<SaveDatabaseMigrationResult>::failure(before.error().code,
                                                                  before.error().message);
    }

    auto snapshot = read_snapshot();
    if (!snapshot) {
        return core::Result<SaveDatabaseMigrationResult>::failure(snapshot.error().code,
                                                                  snapshot.error().message);
    }

    auto migration =
        SaveMigrationRunner::migrate(snapshot.value(), registry, target_schema_version);
    if (!migration) {
        return core::Result<SaveDatabaseMigrationResult>::failure(migration.error().code,
                                                                  migration.error().message);
    }

    SaveDatabaseMigrationResult result;
    result.before = before.value();
    result.migration = std::move(migration).value();

    if (!result.migration.applied_migrations.empty()) {
        auto status = write_snapshot(snapshot.value());
        if (!status) {
            return core::Result<SaveDatabaseMigrationResult>::failure(status.error().code,
                                                                      status.error().message);
        }
        result.wrote_snapshot = true;
    }

    auto after = stats();
    if (!after) {
        return core::Result<SaveDatabaseMigrationResult>::failure(after.error().code,
                                                                  after.error().message);
    }
    result.after = after.value();
    return core::Result<SaveDatabaseMigrationResult>::success(std::move(result));
}

core::Result<SaveDatabaseStats> FileSaveDatabase::stats() const {
    auto save_root = active_save_root(root_);
    if (!save_root) {
        return core::Result<SaveDatabaseStats>::failure(save_root.error().code,
                                                        save_root.error().message);
    }

    SaveDatabaseStats result;
    std::error_code error;
    result.uses_generation_manifest =
        std::filesystem::exists(current_generation_path(root_), error);
    if (error) {
        return core::Result<SaveDatabaseStats>::failure("save_database.stats_failed",
                                                        error.message());
    }
    if (result.uses_generation_manifest) {
        auto active_generation = read_current_generation(root_);
        if (!active_generation) {
            return core::Result<SaveDatabaseStats>::failure(active_generation.error().code,
                                                            active_generation.error().message);
        }
        result.active_generation = std::move(active_generation).value();
    }

    auto generation_stats = collect_generation_directory_stats(root_);
    if (!generation_stats) {
        return core::Result<SaveDatabaseStats>::failure(generation_stats.error().code,
                                                        generation_stats.error().message);
    }
    result.committed_generation_count = generation_stats.value().committed_count;
    result.staged_generation_count = generation_stats.value().staged_count;
    if (result.uses_generation_manifest && result.committed_generation_count > 0) {
        result.stale_generation_count = result.committed_generation_count - 1;
    }

    const auto snapshot = snapshot_path(save_root.value());
    result.has_snapshot = std::filesystem::exists(snapshot, error);
    if (error) {
        return core::Result<SaveDatabaseStats>::failure("save_database.stats_failed",
                                                        error.message());
    }
    if (result.has_snapshot) {
        result.snapshot_bytes = std::filesystem::file_size(snapshot, error);
        if (error) {
            return core::Result<SaveDatabaseStats>::failure("save_database.stats_failed",
                                                            error.message());
        }
    }

    auto entries = read_chunk_index(save_root.value());
    if (!entries) {
        return core::Result<SaveDatabaseStats>::failure(entries.error().code,
                                                        entries.error().message);
    }
    result.chunk_delta_count = entries.value().size();
    for (const auto& entry : entries.value()) {
        result.chunk_delta_bytes +=
            std::filesystem::file_size(chunk_directory(save_root.value()) / entry.filename, error);
        if (error) {
            return core::Result<SaveDatabaseStats>::failure("save_database.stats_failed",
                                                            error.message());
        }
    }

    return core::Result<SaveDatabaseStats>::success(result);
}

} // namespace heartstead::save
