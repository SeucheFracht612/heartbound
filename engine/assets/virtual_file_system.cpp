#include "engine/assets/virtual_file_system.hpp"

#include "engine/core/file_io.hpp"
#include "engine/core/ids.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <system_error>
#include <unordered_set>

namespace heartstead::assets {

namespace {

[[nodiscard]] bool has_bad_virtual_path_text(std::string_view value) {
    return value.empty() || value.find('\\') != std::string_view::npos ||
           value.find("//") != std::string_view::npos;
}

[[nodiscard]] bool is_same_or_child_path(const std::filesystem::path& child,
                                         const std::filesystem::path& parent) {
    auto child_it = child.begin();
    auto parent_it = parent.begin();

    for (; parent_it != parent.end(); ++parent_it, ++child_it) {
        if (child_it == child.end() || *child_it != *parent_it) {
            return false;
        }
    }

    return true;
}

[[nodiscard]] core::Result<std::vector<VirtualFileEntry>>
list_files_impl(const std::vector<MountPoint>& mounts, std::string_view namespace_id,
                const std::filesystem::path* relative_directory, std::string_view display_path,
                VirtualFileListOptions options) {
    if (options.maximum_entries == 0 || options.maximum_depth == 0) {
        return core::Result<std::vector<VirtualFileEntry>>::failure(
            "vfs.invalid_list_limit", "virtual file entry and depth limits must be non-zero");
    }
    std::vector<VirtualFileEntry> entries;
    std::unordered_set<std::string> seen_relative_paths;
    bool namespace_mounted = false;
    std::size_t inspected_entry_count = 0;

    for (std::size_t reverse_index = mounts.size(); reverse_index > 0; --reverse_index) {
        const auto mount_index = reverse_index - 1;
        const auto& mount = mounts[mount_index];
        if (mount.namespace_id != namespace_id) {
            continue;
        }
        namespace_mounted = true;

        std::error_code error;
        const auto target =
            relative_directory == nullptr ? mount.root : mount.root / *relative_directory;
        const auto target_status = std::filesystem::symlink_status(target, error);
        if (error && error != std::errc::no_such_file_or_directory) {
            return core::Result<std::vector<VirtualFileEntry>>::failure(
                "vfs.list_failed", "failed to inspect virtual directory: " + target.string());
        }
        if (!error && std::filesystem::is_symlink(target_status)) {
            return core::Result<std::vector<VirtualFileEntry>>::failure(
                "vfs.symlink_forbidden",
                "virtual directory must not be a symbolic link: " + target.string());
        }
        error.clear();
        const auto candidate = std::filesystem::weakly_canonical(target, error);
        std::error_code directory_error;
        if (error || !is_same_or_child_path(candidate, mount.root) ||
            !std::filesystem::is_directory(candidate, directory_error) || directory_error) {
            continue;
        }

        std::filesystem::recursive_directory_iterator iterator{candidate, error};
        if (error) {
            return core::Result<std::vector<VirtualFileEntry>>::failure(
                "vfs.list_failed",
                "failed to list virtual directory: " + std::string(display_path));
        }

        for (const std::filesystem::recursive_directory_iterator end; iterator != end;
             iterator.increment(error)) {
            if (error) {
                return core::Result<std::vector<VirtualFileEntry>>::failure(
                    "vfs.list_failed",
                    "failed to continue listing virtual directory: " + std::string(display_path));
            }

            ++inspected_entry_count;
            if (inspected_entry_count > options.maximum_entries) {
                return core::Result<std::vector<VirtualFileEntry>>::failure(
                    "vfs.directory_too_large",
                    "virtual directory exceeds its entry limit: " + std::string(display_path));
            }
            if (static_cast<std::size_t>(iterator.depth()) + 1U > options.maximum_depth) {
                return core::Result<std::vector<VirtualFileEntry>>::failure(
                    "vfs.directory_too_deep",
                    "virtual directory exceeds its depth limit: " + std::string(display_path));
            }

            std::error_code entry_error;
            const auto entry_status = iterator->symlink_status(entry_error);
            if (entry_error) {
                return core::Result<std::vector<VirtualFileEntry>>::failure(
                    "vfs.list_failed",
                    "failed to inspect virtual directory entry: " + iterator->path().string());
            }
            if (std::filesystem::is_symlink(entry_status)) {
                return core::Result<std::vector<VirtualFileEntry>>::failure(
                    "vfs.symlink_forbidden",
                    "virtual directory trees must not contain symbolic links: " +
                        iterator->path().string());
            }
            if (!std::filesystem::is_regular_file(entry_status)) {
                continue;
            }

            std::error_code resolve_error;
            const auto resolved_entry =
                std::filesystem::weakly_canonical(iterator->path(), resolve_error);
            if (resolve_error) {
                return core::Result<std::vector<VirtualFileEntry>>::failure(
                    "vfs.list_failed",
                    "failed to resolve virtual directory entry: " + iterator->path().string());
            }
            if (!is_same_or_child_path(resolved_entry, mount.root)) {
                return core::Result<std::vector<VirtualFileEntry>>::failure(
                    "vfs.path_outside_root",
                    "virtual directory entry resolves outside its mount root: " +
                        iterator->path().string());
            }

            const auto relative =
                std::filesystem::relative(resolved_entry, mount.root, resolve_error);
            if (resolve_error) {
                return core::Result<std::vector<VirtualFileEntry>>::failure(
                    "vfs.relative_failed",
                    "failed to compute virtual file path below mount root: " +
                        resolved_entry.string());
            }

            const auto relative_text = relative.generic_string();
            if (!seen_relative_paths.insert(relative_text).second) {
                continue;
            }

            auto parsed = VirtualPath::parse(std::string(namespace_id) + ":" + relative_text);
            if (!parsed) {
                return core::Result<std::vector<VirtualFileEntry>>::failure(parsed.error().code,
                                                                            parsed.error().message);
            }

            entries.push_back(VirtualFileEntry{parsed.value(), resolved_entry, mount_index});
        }
        if (error) {
            return core::Result<std::vector<VirtualFileEntry>>::failure(
                "vfs.list_failed",
                "failed while listing virtual directory: " + std::string(display_path));
        }
    }

    if (!namespace_mounted) {
        return core::Result<std::vector<VirtualFileEntry>>::failure(
            "vfs.namespace_not_mounted",
            "no mount exists for virtual namespace: " + std::string(namespace_id));
    }

    std::ranges::sort(entries, {},
                      [](const VirtualFileEntry& entry) { return entry.virtual_path.to_string(); });
    return core::Result<std::vector<VirtualFileEntry>>::success(std::move(entries));
}

} // namespace

bool is_safe_asset_relative_path(const std::filesystem::path& path) noexcept {
    if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory() ||
        path.generic_string().find('\\') != std::string::npos) {
        return false;
    }

    for (const auto& part : path) {
        const auto text = part.generic_string();
        if (text.empty() || text == "." || text == "..") {
            return false;
        }
    }

    return true;
}

core::Result<std::filesystem::path> canonical_asset_root(const std::filesystem::path& root) {
    if (root.empty()) {
        return core::Result<std::filesystem::path>::failure(
            "asset_path.invalid_root", "asset filesystem root must not be empty");
    }

    std::error_code error;
    const auto absolute_root = std::filesystem::absolute(root, error);
    if (error) {
        return core::Result<std::filesystem::path>::failure(
            "asset_path.root_resolution_failed",
            "failed to resolve asset filesystem root: " + error.message());
    }
    const auto canonical_root = std::filesystem::weakly_canonical(absolute_root, error);
    if (error) {
        return core::Result<std::filesystem::path>::failure(
            "asset_path.root_resolution_failed",
            "failed to canonicalize asset filesystem root: " + error.message());
    }
    return core::Result<std::filesystem::path>::success(canonical_root);
}

core::Result<std::filesystem::path> resolve_asset_path(const std::filesystem::path& root,
                                                       const std::filesystem::path& relative_path) {
    if (!is_safe_asset_relative_path(relative_path)) {
        return core::Result<std::filesystem::path>::failure(
            "asset_path.invalid_relative_path",
            "asset filesystem path must be a safe relative path");
    }

    auto canonical_root = canonical_asset_root(root);
    if (!canonical_root) {
        return core::Result<std::filesystem::path>::failure(canonical_root.error().code,
                                                            canonical_root.error().message);
    }

    std::error_code error;
    const auto candidate =
        std::filesystem::weakly_canonical(canonical_root.value() / relative_path, error);
    if (error) {
        return core::Result<std::filesystem::path>::failure(
            "asset_path.path_resolution_failed",
            "failed to resolve asset filesystem path: " + error.message());
    }
    if (!is_same_or_child_path(candidate, canonical_root.value())) {
        return core::Result<std::filesystem::path>::failure(
            "asset_path.outside_root", "asset filesystem path resolves outside its root");
    }
    return core::Result<std::filesystem::path>::success(candidate);
}

core::Result<VirtualPath> VirtualPath::parse(std::string_view value) {
    const auto separator = value.find(':');
    if (separator == std::string_view::npos || separator == 0 || separator + 1 >= value.size()) {
        return core::Result<VirtualPath>::failure("vfs.invalid_path",
                                                  "virtual paths must look like namespace:path");
    }

    const auto namespace_part = value.substr(0, separator);
    const auto relative_part = value.substr(separator + 1);

    if (!core::is_valid_namespace_id(namespace_part)) {
        return core::Result<VirtualPath>::failure("vfs.invalid_namespace",
                                                  "virtual path namespace is not valid");
    }

    if (has_bad_virtual_path_text(relative_part)) {
        return core::Result<VirtualPath>::failure("vfs.invalid_relative_path",
                                                  "virtual path contains invalid separators");
    }

    std::filesystem::path relative_path{std::string(relative_part)};
    if (!is_safe_asset_relative_path(relative_path)) {
        return core::Result<VirtualPath>::failure("vfs.unsafe_relative_path",
                                                  "virtual path must be a safe relative path");
    }

    return core::Result<VirtualPath>::success(
        VirtualPath{std::string(namespace_part), std::move(relative_path)});
}

std::string VirtualPath::to_string() const {
    return namespace_id + ":" + relative_path.generic_string();
}

core::Status VirtualFileSystem::mount(std::string namespace_id, std::filesystem::path root) {
    if (!core::is_valid_namespace_id(namespace_id)) {
        return core::Status::failure("vfs.invalid_namespace", "mount namespace is not valid");
    }

    std::error_code error;
    auto canonical_root = std::filesystem::weakly_canonical(root, error);
    if (error || !std::filesystem::is_directory(canonical_root, error) || error) {
        return core::Status::failure("vfs.invalid_root",
                                     "mount root does not exist or is not a directory: " +
                                         root.string());
    }

    mounts_.push_back(MountPoint{std::move(namespace_id), std::move(canonical_root)});
    return core::Status::ok();
}

core::Result<std::filesystem::path>
VirtualFileSystem::resolve_existing(const VirtualPath& path) const {
    for (auto it = mounts_.rbegin(); it != mounts_.rend(); ++it) {
        if (it->namespace_id != path.namespace_id) {
            continue;
        }

        std::error_code error;
        const auto candidate =
            std::filesystem::weakly_canonical(it->root / path.relative_path, error);
        if (error) {
            return core::Result<std::filesystem::path>::failure(
                "vfs.resolve_failed",
                "failed to resolve virtual path: " + path.to_string() + ": " + error.message());
        }
        if (!is_same_or_child_path(candidate, it->root)) {
            return core::Result<std::filesystem::path>::failure(
                "vfs.path_outside_root",
                "virtual path resolves outside its mount root: " + path.to_string());
        }

        if (std::filesystem::exists(candidate, error) && !error) {
            return core::Result<std::filesystem::path>::success(candidate);
        }
        if (error) {
            return core::Result<std::filesystem::path>::failure(
                "vfs.resolve_failed",
                "failed to inspect virtual path: " + path.to_string() + ": " + error.message());
        }
    }

    return core::Result<std::filesystem::path>::failure(
        "vfs.not_found", "virtual path not found: " + path.to_string());
}

core::Result<std::filesystem::path>
VirtualFileSystem::resolve_existing(std::string_view path) const {
    auto parsed = VirtualPath::parse(path);
    if (!parsed) {
        return core::Result<std::filesystem::path>::failure(parsed.error().code,
                                                            parsed.error().message);
    }
    return resolve_existing(parsed.value());
}

core::Result<std::vector<VirtualFileEntry>>
VirtualFileSystem::list_files(const VirtualPath& directory, VirtualFileListOptions options) const {
    return list_files_impl(mounts_, directory.namespace_id, &directory.relative_path,
                           directory.to_string(), options);
}

core::Result<std::vector<VirtualFileEntry>>
VirtualFileSystem::list_files(std::string_view directory, VirtualFileListOptions options) const {
    auto parsed = VirtualPath::parse(directory);
    if (!parsed) {
        return core::Result<std::vector<VirtualFileEntry>>::failure(parsed.error().code,
                                                                    parsed.error().message);
    }
    return list_files(parsed.value(), options);
}

core::Result<std::vector<VirtualFileEntry>>
VirtualFileSystem::list_namespace_files(std::string_view namespace_id,
                                        VirtualFileListOptions options) const {
    if (!core::is_valid_namespace_id(namespace_id)) {
        return core::Result<std::vector<VirtualFileEntry>>::failure(
            "vfs.invalid_namespace", "virtual namespace is not valid");
    }
    return list_files_impl(mounts_, namespace_id, nullptr, namespace_id, options);
}

core::Result<std::vector<std::uint8_t>>
VirtualFileSystem::read_bytes(std::string_view path, std::size_t maximum_bytes) const {
    auto resolved = resolve_existing(path);
    if (!resolved) {
        return core::Result<std::vector<std::uint8_t>>::failure(resolved.error().code,
                                                                resolved.error().message);
    }

    auto bytes = core::read_binary_file(resolved.value(), {.maximum_bytes = maximum_bytes});
    if (!bytes) {
        return core::Result<std::vector<std::uint8_t>>::failure(
            bytes.error().code == "core.file_too_large" ? "vfs.file_too_large" : "vfs.read_failed",
            bytes.error().message);
    }
    return bytes;
}

core::Result<std::string> VirtualFileSystem::read_text(std::string_view path,
                                                       std::size_t maximum_bytes) const {
    auto bytes = read_bytes(path, maximum_bytes);
    if (!bytes) {
        return core::Result<std::string>::failure(bytes.error().code, bytes.error().message);
    }

    return core::Result<std::string>::success(
        std::string(bytes.value().begin(), bytes.value().end()));
}

} // namespace heartstead::assets
