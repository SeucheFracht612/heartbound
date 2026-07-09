#pragma once

#include "engine/assets/asset_catalog.hpp"
#include "engine/modding/mod_diagnostic.hpp"
#include "engine/renderer/materials/material_definition.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace heartstead::renderer::materials {

enum class MaterialAssetReferenceKind {
    shader_template,
    texture,
};

struct MaterialAssetReference {
    std::string material_id;
    MaterialAssetReferenceKind kind = MaterialAssetReferenceKind::texture;
    std::string binding_name;
    assets::VirtualPath declared_path;
    std::string logical_id;
    assets::VirtualPath active_path;
    assets::AssetKind active_kind = assets::AssetKind::unknown;
    assets::AssetSourceKind source_kind = assets::AssetSourceKind::mod;
    std::string source_id;
    bool required = true;
    bool overridden = false;
};

struct MaterialAssetValidationResult {
    std::vector<MaterialAssetReference> references;
    std::vector<modding::ModDiagnostic> diagnostics;

    [[nodiscard]] bool has_errors() const noexcept;
    [[nodiscard]] std::size_t count_severity(modding::DiagnosticSeverity severity) const noexcept;
    [[nodiscard]] std::size_t override_count() const noexcept;
};

[[nodiscard]] MaterialAssetValidationResult
validate_material_asset_references(const MaterialRegistry& materials,
                                   const assets::AssetCatalog& asset_catalog);

[[nodiscard]] std::string_view
material_asset_reference_kind_name(MaterialAssetReferenceKind kind) noexcept;

} // namespace heartstead::renderer::materials
