#pragma once

#include "engine/assets/asset_catalog.hpp"
#include "engine/core/result.hpp"
#include "engine/modding/mod_diagnostic.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace heartstead::assets {

enum class ShaderExtensionPoint : std::uint8_t {
    material_template,
    foliage,
    water,
    ore_glow,
    post_processing,
    fog_weather,
    debug_overlay,
    sky,
};

struct ResourcePackManifest {
    std::string id;
    std::string name;
    std::string version;
    std::string description;
    std::filesystem::path root;
    std::string target_namespace = "base";
    std::vector<ShaderExtensionPoint> shader_extensions;
    bool gameplay_content = false;
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

class ResourcePackPolicy {
  public:
    [[nodiscard]] static core::Status validate_manifest(const ResourcePackManifest& manifest);
    [[nodiscard]] static core::Status validate_override(const ResourcePackManifest& manifest,
                                                        const AssetRecord& asset);
    [[nodiscard]] static AssetCatalogBuildResult
    index_assets(AssetCatalog& catalog, const ResourcePackManifest& manifest,
                 std::uint32_t priority,
                 std::size_t maximum_file_bytes = default_maximum_asset_source_bytes);
};

[[nodiscard]] std::string_view shader_extension_point_name(ShaderExtensionPoint point) noexcept;
[[nodiscard]] core::Result<ShaderExtensionPoint>
shader_extension_point_from_name(std::string_view name);

} // namespace heartstead::assets
