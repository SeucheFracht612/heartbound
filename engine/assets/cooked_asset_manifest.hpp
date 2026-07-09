#pragma once

#include "engine/assets/asset_catalog.hpp"
#include "engine/core/result.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace heartstead::assets {

struct CookedAssetRecord {
    std::string logical_id;
    AssetKind kind = AssetKind::unknown;
    VirtualPath source_virtual_path;
    AssetSourceKind source_kind = AssetSourceKind::mod;
    std::string source_id;
    std::string source_hash;
    std::filesystem::path cooked_relative_path;
    std::string cooked_hash;
    std::uint32_t pipeline_version = 1;
    bool active = true;
    std::vector<VirtualPath> dependencies;
};

struct CookedAssetDependencyIssue {
    std::string logical_id;
    VirtualPath dependency;
    std::string code;
    std::string message;
};

struct CookedAssetDependencyReport {
    std::vector<CookedAssetDependencyIssue> issues;

    [[nodiscard]] bool has_errors() const noexcept;
};

struct CookedAssetManifest {
    std::uint32_t schema_version = 1;
    std::string profile = "development";
    std::vector<CookedAssetRecord> records;

    [[nodiscard]] core::Status validate() const;
    [[nodiscard]] const CookedAssetRecord* find(std::string_view logical_id) const noexcept;
    [[nodiscard]] const CookedAssetRecord* find_active(std::string_view logical_id) const noexcept;
    [[nodiscard]] std::vector<const CookedAssetRecord*>
    records_for(std::string_view logical_id) const;
    [[nodiscard]] std::size_t count_kind(AssetKind kind) const noexcept;
    [[nodiscard]] std::size_t active_count() const noexcept;
    [[nodiscard]] CookedAssetDependencyReport dependency_report() const;
    [[nodiscard]] core::Status validate_dependencies() const;
};

struct CookedAssetBuildConfig {
    std::string profile = "development";
    bool active_assets_only = true;
    std::uint32_t pipeline_version = 1;
};

class CookedAssetManifestBuilder {
  public:
    [[nodiscard]] static core::Result<CookedAssetManifest>
    build(const AssetCatalog& catalog, CookedAssetBuildConfig config = {});
};

class CookedAssetManifestTextCodec {
  public:
    [[nodiscard]] static std::string encode(const CookedAssetManifest& manifest);
    [[nodiscard]] static core::Result<CookedAssetManifest> decode(std::string_view text);
};

} // namespace heartstead::assets
