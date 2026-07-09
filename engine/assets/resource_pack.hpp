#pragma once

#include "engine/core/result.hpp"
#include "engine/modding/mod_diagnostic.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace heartstead::assets {

struct ResourcePackManifest {
    std::string id;
    std::string name;
    std::string version;
    std::string description;
    std::filesystem::path root;
};

struct ResourcePackDiscoveryResult {
    std::vector<ResourcePackManifest> packs;
    std::vector<modding::ModDiagnostic> diagnostics;

    [[nodiscard]] bool has_errors() const noexcept;
};

inline constexpr std::uint32_t default_resource_pack_priority_base = 1000;
inline constexpr std::uint32_t default_resource_pack_priority_step = 1;

struct ResourcePackLoadEntry {
    ResourcePackManifest manifest;
    std::size_t load_index = 0;
    std::uint32_t asset_priority = default_resource_pack_priority_base;
};

struct ResourcePackLoadPlan {
    std::vector<ResourcePackLoadEntry> entries;

    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] const ResourcePackLoadEntry* find(std::string_view id) const noexcept;
};

class ResourcePackDiscoverer {
  public:
    [[nodiscard]] ResourcePackDiscoveryResult
    discover(const std::filesystem::path& resource_packs_root) const;
};

class ResourcePackLoadPlanner {
  public:
    [[nodiscard]] static core::Result<ResourcePackLoadPlan>
    plan(std::vector<ResourcePackManifest> packs,
         std::uint32_t priority_base = default_resource_pack_priority_base,
         std::uint32_t priority_step = default_resource_pack_priority_step);
};

} // namespace heartstead::assets
