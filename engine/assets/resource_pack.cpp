#include "engine/assets/resource_pack.hpp"

#include "engine/assets/asset_catalog.hpp"
#include "engine/core/filesystem.hpp"
#include "engine/core/ids.hpp"
#include "engine/modding/flat_manifest.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <set>
#include <string_view>
#include <utility>

namespace heartstead::assets {

namespace {

[[nodiscard]] std::string trim(std::string_view value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return std::string(value.substr(first, last - first + 1));
}

[[nodiscard]] std::string required_value(const std::map<std::string, std::string>& values,
                                         std::string_view key, const std::filesystem::path& source,
                                         std::vector<modding::ModDiagnostic>& diagnostics) {
    const auto found = values.find(std::string(key));
    if (found == values.end() || found->second.empty()) {
        diagnostics.push_back(modding::ModDiagnostic{
            modding::DiagnosticSeverity::error, source, "resource_pack.manifest.missing_field",
            "missing required field: " + std::string(key)});
        return {};
    }
    return found->second;
}

void validate_manifest_fields(const std::map<std::string, std::string>& values,
                              const std::filesystem::path& source,
                              std::vector<modding::ModDiagnostic>& diagnostics) {
    constexpr std::array<std::string_view, 7> known_fields{"id",
                                                           "name",
                                                           "version",
                                                           "description",
                                                           "gameplay",
                                                           "target_namespace",
                                                           "shader_extensions"};
    for (const auto& [key, _] : values) {
        if (std::ranges::find(known_fields, key) == known_fields.end()) {
            diagnostics.push_back(modding::ModDiagnostic{
                modding::DiagnosticSeverity::error, source, "resource_pack.manifest.unknown_field",
                "unknown resource pack manifest field: " + key});
        }
    }
}

[[nodiscard]] bool has_errors_since(const std::vector<modding::ModDiagnostic>& diagnostics,
                                    std::size_t start) noexcept {
    return std::ranges::any_of(diagnostics.begin() + static_cast<std::ptrdiff_t>(start),
                               diagnostics.end(), [](const modding::ModDiagnostic& diagnostic) {
                                   return diagnostic.severity == modding::DiagnosticSeverity::error;
                               });
}

[[nodiscard]] core::Result<ShaderExtensionPoint>
shader_extension_point_from_path(std::string_view path) {
    constexpr std::string_view prefix = "shaders/extensions/";
    if (!path.starts_with(prefix)) {
        return core::Result<ShaderExtensionPoint>::failure(
            "resource_pack.unscoped_shader_forbidden",
            "resource-pack shaders must live below shaders/extensions/<extension>/");
    }
    path.remove_prefix(prefix.size());
    const auto separator = path.find('/');
    if (separator == std::string_view::npos || separator == 0 || separator + 1U >= path.size()) {
        return core::Result<ShaderExtensionPoint>::failure(
            "resource_pack.invalid_shader_extension_path",
            "resource-pack shader path must name an extension point and a file");
    }
    return shader_extension_point_from_name(path.substr(0, separator));
}

} // namespace

std::string_view shader_extension_point_name(ShaderExtensionPoint point) noexcept {
    switch (point) {
    case ShaderExtensionPoint::material_template:
        return "material_template";
    case ShaderExtensionPoint::foliage:
        return "foliage";
    case ShaderExtensionPoint::water:
        return "water";
    case ShaderExtensionPoint::ore_glow:
        return "ore_glow";
    case ShaderExtensionPoint::post_processing:
        return "post_processing";
    case ShaderExtensionPoint::fog_weather:
        return "fog_weather";
    case ShaderExtensionPoint::debug_overlay:
        return "debug_overlay";
    case ShaderExtensionPoint::sky:
        return "sky";
    }
    return "unknown";
}

core::Result<ShaderExtensionPoint> shader_extension_point_from_name(std::string_view name) {
    if (name == "material_template")
        return core::Result<ShaderExtensionPoint>::success(ShaderExtensionPoint::material_template);
    if (name == "foliage")
        return core::Result<ShaderExtensionPoint>::success(ShaderExtensionPoint::foliage);
    if (name == "water")
        return core::Result<ShaderExtensionPoint>::success(ShaderExtensionPoint::water);
    if (name == "ore_glow")
        return core::Result<ShaderExtensionPoint>::success(ShaderExtensionPoint::ore_glow);
    if (name == "post_processing")
        return core::Result<ShaderExtensionPoint>::success(ShaderExtensionPoint::post_processing);
    if (name == "fog_weather")
        return core::Result<ShaderExtensionPoint>::success(ShaderExtensionPoint::fog_weather);
    if (name == "debug_overlay")
        return core::Result<ShaderExtensionPoint>::success(ShaderExtensionPoint::debug_overlay);
    if (name == "sky")
        return core::Result<ShaderExtensionPoint>::success(ShaderExtensionPoint::sky);
    return core::Result<ShaderExtensionPoint>::failure(
        "resource_pack.unknown_shader_extension",
        "shader extension point is not controlled by the engine");
}

core::Status ResourcePackPolicy::validate_manifest(const ResourcePackManifest& manifest) {
    if (!core::is_valid_namespace_id(manifest.id) ||
        !core::is_valid_namespace_id(manifest.target_namespace) || manifest.name.empty() ||
        manifest.version.empty()) {
        return core::Status::failure("resource_pack.invalid_manifest",
                                     "resource pack id, name, and version are required");
    }
    if (manifest.gameplay_content) {
        return core::Status::failure(
            "resource_pack.gameplay_forbidden",
            "resource packs cannot declare gameplay content; use a gameplay mod");
    }
    std::set<ShaderExtensionPoint> extensions;
    for (const auto extension : manifest.shader_extensions) {
        if (shader_extension_point_name(extension) == "unknown") {
            return core::Status::failure("resource_pack.unknown_shader_extension",
                                         "shader extension point is not controlled by the engine");
        }
        if (!extensions.insert(extension).second) {
            return core::Status::failure("resource_pack.duplicate_shader_extension",
                                         "shader extension points must be unique");
        }
    }
    return core::Status::ok();
}

core::Status ResourcePackPolicy::validate_override(const ResourcePackManifest& manifest,
                                                   const AssetRecord& asset) {
    auto status = validate_manifest(manifest);
    if (!status)
        return status;
    if (asset.source_kind != AssetSourceKind::resource_pack || asset.source_id != manifest.id) {
        return core::Status::failure("resource_pack.asset_source_mismatch",
                                     "asset does not belong to the resource pack being validated");
    }
    if (!core::PrototypeId::parse(asset.logical_id)) {
        return core::Status::failure("resource_pack.invalid_asset_id",
                                     "resource-pack asset logical id is invalid");
    }
    if (asset.virtual_path.namespace_id != manifest.target_namespace ||
        asset.logical_id != asset_logical_id(asset.virtual_path)) {
        return core::Status::failure("resource_pack.asset_namespace_mismatch",
                                     "resource-pack assets must use the manifest target namespace");
    }
    const auto path = asset.virtual_path.relative_path.generic_string();
    constexpr std::array<std::string_view, 5> forbidden{{
        "prototypes/",
        "scripts/",
        "migrations/",
        "server/",
        "worldgen/",
    }};
    if (std::ranges::any_of(
            forbidden, [&path](std::string_view prefix) { return path.starts_with(prefix); })) {
        return core::Status::failure(
            "resource_pack.gameplay_path_forbidden",
            "resource packs cannot override gameplay, server, migration, or worldgen data");
    }
    if (asset.kind == AssetKind::data || asset.kind == AssetKind::unknown) {
        return core::Status::failure(
            "resource_pack.non_presentation_asset_forbidden",
            "resource packs may contain only recognized presentation asset kinds");
    }
    if (asset.kind == AssetKind::shader &&
        (asset.virtual_path.relative_path.extension() == ".spv" || path.contains("raw_vulkan"))) {
        return core::Status::failure(
            "resource_pack.raw_vulkan_forbidden",
            "shader packs must use controlled engine shader extension points");
    }
    if (asset.kind == AssetKind::shader) {
        auto extension = shader_extension_point_from_path(path);
        if (!extension) {
            return core::Status::failure(extension.error().code, extension.error().message);
        }
        if (std::ranges::find(manifest.shader_extensions, extension.value()) ==
            manifest.shader_extensions.end()) {
            return core::Status::failure(
                "resource_pack.undeclared_shader_extension",
                "resource pack shader uses an extension point not declared by its manifest: " +
                    std::string(shader_extension_point_name(extension.value())));
        }
    }
    return core::Status::ok();
}

AssetCatalogBuildResult ResourcePackPolicy::index_assets(AssetCatalog& catalog,
                                                         const ResourcePackManifest& manifest,
                                                         std::uint32_t priority,
                                                         std::size_t maximum_file_bytes) {
    AssetCatalogBuildResult result;
    if (auto status = validate_manifest(manifest); !status) {
        result.diagnostics.push_back(modding::ModDiagnostic{
            modding::DiagnosticSeverity::error,
            manifest.root / "resource_pack.toml",
            status.error().code,
            status.error().message,
        });
        return result;
    }

    auto staged_catalog = catalog;
    result = AssetCatalogBuilder::index_directory(
        staged_catalog, manifest.root / "assets", manifest.target_namespace,
        AssetSourceKind::resource_pack, manifest.id, priority, maximum_file_bytes);
    for (const auto* asset : staged_catalog.records()) {
        if (asset->source_kind != AssetSourceKind::resource_pack ||
            asset->source_id != manifest.id) {
            continue;
        }
        if (auto status = validate_override(manifest, *asset); !status) {
            result.diagnostics.push_back(modding::ModDiagnostic{
                modding::DiagnosticSeverity::error,
                asset->source_path,
                status.error().code,
                status.error().message,
            });
        }
    }
    if (!result.has_errors()) {
        catalog = std::move(staged_catalog);
    }
    return result;
}

bool ResourcePackDiscoveryResult::has_errors() const noexcept {
    return std::ranges::any_of(diagnostics, [](const modding::ModDiagnostic& diagnostic) {
        return diagnostic.severity == modding::DiagnosticSeverity::error;
    });
}

bool ResourcePackLoadPlan::empty() const noexcept {
    return entries.empty();
}

std::size_t ResourcePackLoadPlan::size() const noexcept {
    return entries.size();
}

const ResourcePackLoadEntry* ResourcePackLoadPlan::find(std::string_view id) const noexcept {
    const auto found = std::ranges::find_if(
        entries, [id](const ResourcePackLoadEntry& entry) { return entry.manifest.id == id; });
    return found == entries.end() ? nullptr : &*found;
}

ResourcePackDiscoveryResult
ResourcePackDiscoverer::discover(const std::filesystem::path& resource_packs_root) const {
    ResourcePackDiscoveryResult result;

    auto pack_directories = core::list_directories(resource_packs_root);
    if (!pack_directories) {
        const auto missing = pack_directories.error().code == "core.directory_not_directory";
        result.diagnostics.push_back(modding::ModDiagnostic{
            modding::DiagnosticSeverity::error,
            resource_packs_root,
            missing ? "resource_pack.root.missing" : pack_directories.error().code,
            pack_directories.error().message,
        });
        return result;
    }

    std::set<std::string> seen_ids;
    for (const auto& pack_directory : pack_directories.value()) {
        const auto manifest_path = pack_directory / "resource_pack.toml";
        std::error_code manifest_error;
        const auto manifest_status = std::filesystem::symlink_status(manifest_path, manifest_error);
        if (manifest_error == std::errc::no_such_file_or_directory ||
            (!manifest_error && !std::filesystem::exists(manifest_status))) {
            result.diagnostics.push_back(
                modding::ModDiagnostic{modding::DiagnosticSeverity::warning, pack_directory,
                                       "resource_pack.manifest.missing",
                                       "directory does not contain "
                                       "resource_pack.toml"});
            continue;
        }
        if (manifest_error || std::filesystem::is_symlink(manifest_status) ||
            !std::filesystem::is_regular_file(manifest_status)) {
            result.diagnostics.push_back(modding::ModDiagnostic{
                modding::DiagnosticSeverity::error,
                manifest_path,
                "resource_pack.manifest.unsafe_file",
                "resource pack manifest must be a readable non-symlink regular file",
            });
            continue;
        }

        const auto diagnostic_start = result.diagnostics.size();
        const auto values = modding::parse_flat_manifest(
            manifest_path, result.diagnostics, {.diagnostic_prefix = "resource_pack.manifest"});
        validate_manifest_fields(values, manifest_path, result.diagnostics);
        ResourcePackManifest manifest;
        manifest.id = required_value(values, "id", manifest_path, result.diagnostics);
        manifest.name = required_value(values, "name", manifest_path, result.diagnostics);
        manifest.version = required_value(values, "version", manifest_path, result.diagnostics);
        manifest.description = values.contains("description") ? values.at("description") : "";
        manifest.root = pack_directory;
        manifest.target_namespace = values.contains("target_namespace")
                                        ? values.at("target_namespace")
                                        : std::string("base");
        if (const auto gameplay = values.find("gameplay"); gameplay != values.end()) {
            if (gameplay->second == "true") {
                manifest.gameplay_content = true;
            } else if (gameplay->second != "false") {
                result.diagnostics.push_back(
                    modding::ModDiagnostic{modding::DiagnosticSeverity::error, manifest_path,
                                           "resource_pack.manifest.invalid_bool",
                                           "resource pack gameplay field must be true or false"});
            }
        }
        if (const auto shader_extensions = values.find("shader_extensions");
            shader_extensions != values.end() && !shader_extensions->second.empty()) {
            constexpr std::size_t max_shader_extensions = 32;
            if (static_cast<std::size_t>(std::ranges::count(shader_extensions->second, ',')) >=
                max_shader_extensions) {
                result.diagnostics.push_back(
                    modding::ModDiagnostic{modding::DiagnosticSeverity::error, manifest_path,
                                           "resource_pack.manifest.too_many_shader_extensions",
                                           "resource pack exceeds its shader extension limit"});
                continue;
            }
            std::size_t start = 0;
            while (start <= shader_extensions->second.size()) {
                const auto end = shader_extensions->second.find(',', start);
                const auto token =
                    end == std::string::npos
                        ? std::string_view(shader_extensions->second).substr(start)
                        : std::string_view(shader_extensions->second).substr(start, end - start);
                auto extension = shader_extension_point_from_name(trim(std::string(token)));
                if (!extension) {
                    result.diagnostics.push_back(
                        modding::ModDiagnostic{modding::DiagnosticSeverity::error, manifest_path,
                                               extension.error().code, extension.error().message});
                } else {
                    manifest.shader_extensions.push_back(extension.value());
                }
                if (end == std::string::npos)
                    break;
                start = end + 1;
            }
        }

        if (!core::is_valid_namespace_id(manifest.id)) {
            result.diagnostics.push_back(modding::ModDiagnostic{modding::DiagnosticSeverity::error,
                                                                manifest_path,
                                                                "resource_pack.manifest.invalid_id",
                                                                "resource pack id must be a valid "
                                                                "namespace id"});
        }

        if (!has_errors_since(result.diagnostics, diagnostic_start)) {
            if (auto status = ResourcePackPolicy::validate_manifest(manifest); !status) {
                result.diagnostics.push_back(
                    modding::ModDiagnostic{modding::DiagnosticSeverity::error, manifest_path,
                                           status.error().code, status.error().message});
            }
        }

        if (has_errors_since(result.diagnostics, diagnostic_start)) {
            continue;
        }

        if (!seen_ids.insert(manifest.id).second) {
            result.diagnostics.push_back(
                modding::ModDiagnostic{modding::DiagnosticSeverity::error, manifest_path,
                                       "resource_pack.manifest.duplicate_id",
                                       "duplicate resource pack id: " + manifest.id});
            continue;
        }

        result.packs.push_back(std::move(manifest));
    }

    std::ranges::sort(result.packs, {}, &ResourcePackManifest::id);
    return result;
}

core::Result<ResourcePackLoadPlan>
ResourcePackLoadPlanner::plan(std::vector<ResourcePackManifest> packs, std::uint32_t priority_base,
                              std::uint32_t priority_step) {
    if (priority_step == 0) {
        return core::Result<ResourcePackLoadPlan>::failure(
            "resource_pack_load_plan.invalid_priority_step",
            "resource pack priority step must be non-zero");
    }

    std::ranges::sort(packs, {}, &ResourcePackManifest::id);

    std::set<std::string> seen_ids;
    ResourcePackLoadPlan plan;
    plan.entries.reserve(packs.size());
    for (std::size_t index = 0; index < packs.size(); ++index) {
        auto& pack = packs[index];
        auto policy_status = ResourcePackPolicy::validate_manifest(pack);
        if (!policy_status) {
            return core::Result<ResourcePackLoadPlan>::failure(policy_status.error().code,
                                                               policy_status.error().message);
        }
        if (!core::is_valid_namespace_id(pack.id)) {
            return core::Result<ResourcePackLoadPlan>::failure(
                "resource_pack_load_plan.invalid_id",
                "resource pack load plan contains an invalid pack id");
        }
        if (!seen_ids.insert(pack.id).second) {
            return core::Result<ResourcePackLoadPlan>::failure(
                "resource_pack_load_plan.duplicate_id",
                "resource pack load plan contains a duplicate pack id: " + pack.id);
        }

        const auto max_priority = std::numeric_limits<std::uint32_t>::max();
        if (index > (max_priority - priority_base) / priority_step) {
            return core::Result<ResourcePackLoadPlan>::failure(
                "resource_pack_load_plan.priority_overflow",
                "resource pack load plan priority range overflows uint32");
        }

        plan.entries.push_back(ResourcePackLoadEntry{
            std::move(pack),
            index,
            static_cast<std::uint32_t>(priority_base + (index * priority_step)),
        });
    }

    return core::Result<ResourcePackLoadPlan>::success(std::move(plan));
}

} // namespace heartstead::assets
