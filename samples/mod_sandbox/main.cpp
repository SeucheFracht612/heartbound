#include "engine/assets/asset_catalog.hpp"
#include "engine/assets/resource_pack.hpp"
#include "engine/assets/virtual_file_system.hpp"
#include "engine/core/logging.hpp"
#include "engine/core/process_entry.hpp"
#include "engine/modding/generic_prototype_loader.hpp"
#include "engine/modding/mod_discovery.hpp"
#include "engine/modding/prototype_registry.hpp"
#include "engine/renderer/materials/material_asset_validation.hpp"
#include "engine/renderer/materials/material_prototype_loader.hpp"
#include "engine/world/voxels/voxel_palette.hpp"

#include <filesystem>
#include <string>
#include <utility>

namespace {

void log_diagnostic(const heartstead::modding::ModDiagnostic& diagnostic) {
    using heartstead::core::LogLevel;
    using heartstead::modding::DiagnosticSeverity;

    const auto level = diagnostic.severity == DiagnosticSeverity::error     ? LogLevel::error
                       : diagnostic.severity == DiagnosticSeverity::warning ? LogLevel::warning
                                                                            : LogLevel::info;

    heartstead::core::log(level, diagnostic.code + ": " + diagnostic.message + " (" +
                                     diagnostic.source.generic_string() + ")");
}

} // namespace

int main(int argc, char** argv) {
    return heartstead::core::run_no_argument_process_entry(argc, argv, [] {
        using namespace heartstead;

        const std::filesystem::path source_root = HEARTSTEAD_SOURCE_ROOT;
        const auto mods_root = source_root / "mods";
        const auto resource_packs_root = source_root / "resource_packs";

        core::log(core::LogLevel::info, "Discovering mods in " + mods_root.string());

        modding::ModDiscoverer discoverer;
        auto discovery = discoverer.discover(mods_root);
        for (const auto& diagnostic : discovery.diagnostics) {
            log_diagnostic(diagnostic);
        }
        if (discovery.has_errors()) {
            return 1;
        }

        assets::VirtualFileSystem vfs;
        assets::AssetCatalog asset_catalog;
        for (const auto& mod : discovery.mods) {
            const auto assets_root = mod.root / "assets";
            if (std::filesystem::is_directory(assets_root)) {
                auto status = vfs.mount(mod.id, assets_root);
                if (!status) {
                    core::log(core::LogLevel::error, status.error().message);
                    return 1;
                }
                auto indexed = assets::AssetCatalogBuilder::index_directory(
                    asset_catalog, assets_root, mod.id, assets::AssetSourceKind::mod, mod.id, 0);
                for (const auto& diagnostic : indexed.diagnostics) {
                    log_diagnostic(diagnostic);
                }
                if (indexed.has_errors()) {
                    return 1;
                }
            }
            core::log(core::LogLevel::info, "Loaded mod " + mod.id + " " + mod.version);
        }

        assets::ResourcePackDiscoverer resource_pack_discoverer;
        auto resource_packs = resource_pack_discoverer.discover(resource_packs_root);
        for (const auto& diagnostic : resource_packs.diagnostics) {
            log_diagnostic(diagnostic);
        }
        if (resource_packs.has_errors()) {
            return 1;
        }

        auto resource_pack_plan = assets::ResourcePackLoadPlanner::plan(resource_packs.packs);
        if (!resource_pack_plan) {
            core::log(core::LogLevel::error, resource_pack_plan.error().message);
            return 1;
        }

        for (const auto& entry : resource_pack_plan.value().entries) {
            const auto& pack = entry.manifest;
            const auto assets_root = pack.root / "assets";
            if (std::filesystem::is_directory(assets_root)) {
                auto status = vfs.mount(pack.id, assets_root);
                if (!status) {
                    core::log(core::LogLevel::error, status.error().message);
                    return 1;
                }
                auto indexed = assets::AssetCatalogBuilder::index_directory(
                    asset_catalog, assets_root, pack.target_namespace,
                    assets::AssetSourceKind::resource_pack, pack.id, entry.asset_priority);
                for (const auto& diagnostic : indexed.diagnostics) {
                    log_diagnostic(diagnostic);
                }
                if (indexed.has_errors()) {
                    return 1;
                }
            }
            core::log(core::LogLevel::info, "Loaded resource pack " + pack.id + " " + pack.version);
        }

        modding::GenericPrototypeLoader prototype_loader;
        auto prototypes = prototype_loader.load_from_mods(discovery.mods);
        for (const auto& diagnostic : prototypes.diagnostics) {
            log_diagnostic(diagnostic);
        }
        if (prototypes.has_errors()) {
            return 1;
        }

        modding::PrototypeRegistry registry;
        auto registry_build = registry.build(std::move(prototypes.prototypes));
        for (const auto& diagnostic : registry_build.diagnostics) {
            log_diagnostic(diagnostic);
        }
        if (registry_build.has_errors()) {
            return 1;
        }

        for (const auto& prototype : registry.prototypes_of_kind(modding::PrototypeKinds::item)) {
            core::log(core::LogLevel::info, "Item prototype " + prototype->id.value() +
                                                " display=" + prototype->display_name);
        }
        for (const auto& prototype :
             registry.prototypes_of_kind(modding::PrototypeKinds::scenario)) {
            core::log(core::LogLevel::info, "Scenario prototype " + prototype->id.value() +
                                                " display=" + prototype->display_name);
        }

        auto voxel_palette = world::voxel_palette_from_prototypes(registry);
        if (!voxel_palette) {
            core::log(core::LogLevel::error, voxel_palette.error().message);
            return 1;
        }
        for (const auto* definition : voxel_palette.value().definitions()) {
            core::log(core::LogLevel::info, "Voxel type " + std::to_string(definition->type) +
                                                " prototype=" + definition->prototype_id.value());
        }

        auto material_registry = renderer::materials::material_registry_from_prototypes(registry);
        if (!material_registry) {
            core::log(core::LogLevel::error, material_registry.error().message);
            return 1;
        }
        core::log(core::LogLevel::info,
                  "Material definitions: " + std::to_string(material_registry.value().size()));
        auto material_asset_report = renderer::materials::validate_material_asset_references(
            material_registry.value(), asset_catalog);
        for (const auto& diagnostic : material_asset_report.diagnostics) {
            log_diagnostic(diagnostic);
        }
        if (material_asset_report.has_errors()) {
            return 1;
        }
        core::log(core::LogLevel::info,
                  "Material asset references: " +
                      std::to_string(material_asset_report.references.size()) +
                      ", overrides=" + std::to_string(material_asset_report.override_count()));

        core::log(core::LogLevel::info,
                  "Registered prototypes: " + std::to_string(registry.size()));
        core::log(core::LogLevel::info,
                  "Active assets: " + std::to_string(asset_catalog.active_count()));

        const auto* raw_clay_asset = asset_catalog.find_active("base:textures/items/raw_clay.txt");
        if (raw_clay_asset != nullptr) {
            core::log(core::LogLevel::info,
                      "Active raw clay asset " + raw_clay_asset->virtual_path.to_string() +
                          " source=" +
                          std::string(assets::asset_source_kind_name(raw_clay_asset->source_kind)));
        }

        auto raw_clay_texture = vfs.resolve_existing("base:textures/items/raw_clay.txt");
        if (raw_clay_texture) {
            core::log(core::LogLevel::info,
                      "Resolved sample asset " + raw_clay_texture.value().generic_string());
        } else {
            core::log(core::LogLevel::warning, raw_clay_texture.error().message);
        }

        auto clay_voxel_texture = vfs.resolve_existing("base:textures/voxels/clay.txt");
        if (clay_voxel_texture) {
            core::log(core::LogLevel::info, "Resolved clay material asset " +
                                                clay_voxel_texture.value().generic_string());
        } else {
            core::log(core::LogLevel::warning, clay_voxel_texture.error().message);
        }

        return 0;
    });
}
