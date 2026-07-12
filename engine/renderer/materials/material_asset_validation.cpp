#include "engine/renderer/materials/material_asset_validation.hpp"

#include <algorithm>
#include <utility>

namespace heartstead::renderer::materials {

namespace {

[[nodiscard]] std::string logical_id_for(const assets::VirtualPath& path) {
    return assets::asset_logical_id(path);
}

void add_diagnostic(MaterialAssetValidationResult& result, modding::DiagnosticSeverity severity,
                    std::string code, std::string message,
                    const std::filesystem::path& source = {}) {
    result.diagnostics.push_back(modding::ModDiagnostic{
        severity,
        source,
        std::move(code),
        std::move(message),
    });
}

void validate_reference(MaterialAssetValidationResult& result, const MaterialDefinition& material,
                        MaterialAssetReferenceKind kind, std::string_view binding_name,
                        const assets::VirtualPath& declared_path, bool required,
                        assets::AssetKind expected_kind, const assets::AssetCatalog& catalog) {
    const auto logical_id = logical_id_for(declared_path);
    const auto* active = catalog.find_active(logical_id);
    if (active == nullptr) {
        if (required) {
            const auto code = kind == MaterialAssetReferenceKind::shader_template
                                  ? "material_assets.missing_shader_template"
                                  : "material_assets.missing_texture";
            add_diagnostic(result, modding::DiagnosticSeverity::error, code,
                           material.id.value() + ": missing required material asset " +
                               declared_path.to_string());
        } else {
            add_diagnostic(result, modding::DiagnosticSeverity::warning,
                           "material_assets.optional_texture_missing",
                           material.id.value() + ": optional material texture is missing " +
                               declared_path.to_string());
        }
        return;
    }

    if (active->kind != expected_kind) {
        add_diagnostic(result, modding::DiagnosticSeverity::error,
                       "material_assets.wrong_asset_kind",
                       material.id.value() + ": material asset " + declared_path.to_string() +
                           " resolved to " + std::string(assets::asset_kind_name(active->kind)) +
                           " but expected " + std::string(assets::asset_kind_name(expected_kind)),
                       active->source_path);
        return;
    }

    result.references.push_back(MaterialAssetReference{
        material.id.value(),
        kind,
        std::string(binding_name),
        declared_path,
        logical_id,
        active->virtual_path,
        active->kind,
        active->source_kind,
        active->source_id,
        required,
        active->source_kind == assets::AssetSourceKind::resource_pack,
    });
}

} // namespace

bool MaterialAssetValidationResult::has_errors() const noexcept {
    return std::ranges::any_of(diagnostics, [](const modding::ModDiagnostic& diagnostic) {
        return diagnostic.severity == modding::DiagnosticSeverity::error;
    });
}

std::size_t
MaterialAssetValidationResult::count_severity(modding::DiagnosticSeverity severity) const noexcept {
    return static_cast<std::size_t>(
        std::ranges::count_if(diagnostics, [severity](const modding::ModDiagnostic& diagnostic) {
            return diagnostic.severity == severity;
        }));
}

std::size_t MaterialAssetValidationResult::override_count() const noexcept {
    return static_cast<std::size_t>(std::ranges::count_if(
        references, [](const MaterialAssetReference& reference) { return reference.overridden; }));
}

MaterialAssetValidationResult
validate_material_asset_references(const MaterialRegistry& materials,
                                   const assets::AssetCatalog& asset_catalog) {
    MaterialAssetValidationResult result;
    for (const auto* material : materials.definitions()) {
        validate_reference(result, *material, MaterialAssetReferenceKind::shader_template,
                           "shader_template", material->shader_template, true,
                           assets::AssetKind::shader, asset_catalog);

        for (const auto& texture : material->textures) {
            validate_reference(result, *material, MaterialAssetReferenceKind::texture, texture.name,
                               texture.texture, texture.required, assets::AssetKind::texture,
                               asset_catalog);
        }
    }
    return result;
}

std::string_view material_asset_reference_kind_name(MaterialAssetReferenceKind kind) noexcept {
    switch (kind) {
    case MaterialAssetReferenceKind::shader_template:
        return "shader_template";
    case MaterialAssetReferenceKind::texture:
        return "texture";
    }
    return "unknown";
}

} // namespace heartstead::renderer::materials
