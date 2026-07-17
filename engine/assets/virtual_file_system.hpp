#pragma once

#include "engine/core/result.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace heartstead::assets {

inline constexpr std::size_t default_virtual_file_read_limit = 256U * 1024U * 1024U;

struct VirtualPath {
    std::string namespace_id;
    std::filesystem::path relative_path;

    [[nodiscard]] static core::Result<VirtualPath> parse(std::string_view value);
    [[nodiscard]] std::string to_string() const;
};

struct MountPoint {
    std::string namespace_id;
    std::filesystem::path root;
};

struct VirtualFileEntry {
    VirtualPath virtual_path;
    std::filesystem::path resolved_path;
    std::size_t mount_index = 0;
};

struct VirtualFileListOptions {
    std::size_t maximum_entries = 65'536;
    std::size_t maximum_depth = 32;
};

// Asset-facing filesystem paths must stay below an explicit root. These helpers reject
// traversal syntax before resolving symlinks, then verify the resolved path component-wise.
[[nodiscard]] bool is_safe_asset_relative_path(const std::filesystem::path& path) noexcept;
[[nodiscard]] core::Result<std::filesystem::path>
canonical_asset_root(const std::filesystem::path& root);
[[nodiscard]] core::Result<std::filesystem::path>
resolve_asset_path(const std::filesystem::path& root, const std::filesystem::path& relative_path);

class VirtualFileSystem {
  public:
    [[nodiscard]] core::Status mount(std::string namespace_id, std::filesystem::path root);
    [[nodiscard]] core::Result<std::filesystem::path>
    resolve_existing(const VirtualPath& path) const;
    [[nodiscard]] core::Result<std::filesystem::path> resolve_existing(std::string_view path) const;
    [[nodiscard]] core::Result<std::vector<VirtualFileEntry>>
    list_files(const VirtualPath& directory, VirtualFileListOptions options = {}) const;
    [[nodiscard]] core::Result<std::vector<VirtualFileEntry>>
    list_files(std::string_view directory, VirtualFileListOptions options = {}) const;
    [[nodiscard]] core::Result<std::vector<VirtualFileEntry>>
    list_namespace_files(std::string_view namespace_id, VirtualFileListOptions options = {}) const;
    [[nodiscard]] core::Result<std::vector<std::uint8_t>>
    read_bytes(std::string_view path,
               std::size_t maximum_bytes = default_virtual_file_read_limit) const;
    [[nodiscard]] core::Result<std::string>
    read_text(std::string_view path,
              std::size_t maximum_bytes = default_virtual_file_read_limit) const;

    [[nodiscard]] const std::vector<MountPoint>& mounts() const noexcept {
        return mounts_;
    }

  private:
    std::vector<MountPoint> mounts_;
};

} // namespace heartstead::assets
