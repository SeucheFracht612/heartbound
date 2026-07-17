#pragma once

#include "engine/assets/cooked_asset_manifest.hpp"
#include "engine/core/result.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace heartstead::assets {

inline constexpr std::size_t default_maximum_cooked_manifest_bytes = 64U * 1024U * 1024U;
inline constexpr std::size_t default_maximum_cooked_payload_bytes = 257U * 1024U * 1024U;

struct CookedAssetStoreLoadOptions {
    std::size_t maximum_manifest_bytes = default_maximum_cooked_manifest_bytes;
    std::size_t maximum_payload_bytes = default_maximum_cooked_payload_bytes;
};

struct CookedAssetPayload {
    std::string logical_id;
    AssetKind kind = AssetKind::unknown;
    std::string backend;
    std::string profile;
    std::filesystem::path source_path;
    std::map<std::string, std::string> metadata;
    std::vector<std::uint8_t> bytes;
};

class CookedAssetPayloadCodec {
  public:
    [[nodiscard]] static core::Result<CookedAssetPayload>
    decode(std::span<const std::uint8_t> bytes, const CookedAssetRecord& expected,
           std::string_view expected_profile,
           std::size_t maximum_bytes = default_maximum_cooked_payload_bytes);
};

class CookedAssetStore {
  public:
    [[nodiscard]] static core::Result<CookedAssetStore>
    load(std::filesystem::path root,
         std::filesystem::path manifest_relative_path = "asset_manifest.txt",
         CookedAssetStoreLoadOptions options = {});

    [[nodiscard]] const CookedAssetManifest& manifest() const noexcept;
    [[nodiscard]] const std::filesystem::path& root() const noexcept;
    [[nodiscard]] core::Result<CookedAssetPayload> load_payload(std::string_view logical_id) const;
    [[nodiscard]] core::Result<CookedAssetPayload>
    load_payload(const CookedAssetRecord& record) const;

  private:
    std::filesystem::path root_;
    CookedAssetManifest manifest_;
    std::size_t maximum_payload_bytes_ = default_maximum_cooked_payload_bytes;
};

} // namespace heartstead::assets
