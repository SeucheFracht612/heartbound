#include "engine/assets/resource_pack.hpp"

#include "engine/assets/asset_catalog.hpp"
#include "engine/core/ids.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <string_view>
#include <utility>

namespace heartstead::assets {

namespace {

[[nodiscard]] std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

[[nodiscard]] std::string unquote(std::string value) {
    value = trim(std::move(value));
    if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') ||
                              (value.front() == '\'' && value.back() == '\''))) {
        return value.substr(1, value.size() - 2);
    }
    return value;
}

[[nodiscard]] std::map<std::string, std::string>
parse_flat_toml(const std::filesystem::path& file,
                std::vector<modding::ModDiagnostic>& diagnostics) {
    std::ifstream input(file);
    if (!input) {
        diagnostics.push_back(modding::ModDiagnostic{modding::DiagnosticSeverity::error, file,
                                                     "resource_pack.manifest.unreadable",
                                                     "could not read resource pack manifest"});
        return {};
    }

    std::map<std::string, std::string> values;
    std::string line;
    int line_number = 0;

    while (std::getline(input, line)) {
        ++line_number;
        const auto comment = line.find('#');
        if (comment != std::string::npos) {
            line = line.substr(0, comment);
        }

        line = trim(std::move(line));
        if (line.empty()) {
            continue;
        }

        const auto separator = line.find('=');
        if (separator == std::string::npos) {
            diagnostics.push_back(modding::ModDiagnostic{
                modding::DiagnosticSeverity::error,
                file,
                "resource_pack.manifest.syntax",
                "expected key = value at line " + std::to_string(line_number),
            });
            continue;
        }

        auto key = trim(line.substr(0, separator));
        auto value = unquote(line.substr(separator + 1));
        values[std::move(key)] = std::move(value);
    }

    return values;
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
    if (!core::is_valid_namespace_id(manifest.id) || manifest.name.empty() ||
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
    if (asset.kind == AssetKind::shader &&
        (asset.source_path.extension() == ".spv" || path.contains("raw_vulkan"))) {
        return core::Status::failure(
            "resource_pack.raw_vulkan_forbidden",
            "shader packs must use controlled engine shader extension points");
    }
    return core::Status::ok();
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

    if (!std::filesystem::is_directory(resource_packs_root)) {
        result.diagnostics.push_back(modding::ModDiagnostic{
            modding::DiagnosticSeverity::error, resource_packs_root, "resource_pack.root.missing",
            "resource packs root does not exist"});
        return result;
    }

    std::set<std::string> seen_ids;
    for (const auto& entry : std::filesystem::directory_iterator(resource_packs_root)) {
        if (!entry.is_directory()) {
            continue;
        }

        const auto manifest_path = entry.path() / "resource_pack.toml";
        if (!std::filesystem::exists(manifest_path)) {
            result.diagnostics.push_back(
                modding::ModDiagnostic{modding::DiagnosticSeverity::warning, entry.path(),
                                       "resource_pack.manifest.missing",
                                       "directory does not contain "
                                       "resource_pack.toml"});
            continue;
        }

        const auto values = parse_flat_toml(manifest_path, result.diagnostics);
        ResourcePackManifest manifest;
        manifest.id = required_value(values, "id", manifest_path, result.diagnostics);
        manifest.name = required_value(values, "name", manifest_path, result.diagnostics);
        manifest.version = required_value(values, "version", manifest_path, result.diagnostics);
        manifest.description = values.contains("description") ? values.at("description") : "";
        manifest.root = entry.path();
        manifest.gameplay_content = values.contains("gameplay") && values.at("gameplay") == "true";
        if (const auto shader_extensions = values.find("shader_extensions");
            shader_extensions != values.end() && !shader_extensions->second.empty()) {
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

        if (!manifest.id.empty() && !seen_ids.insert(manifest.id).second) {
            result.diagnostics.push_back(
                modding::ModDiagnostic{modding::DiagnosticSeverity::error, manifest_path,
                                       "resource_pack.manifest.duplicate_id",
                                       "duplicate resource pack id: " + manifest.id});
        }

        if (!manifest.id.empty()) {
            if (auto status = ResourcePackPolicy::validate_manifest(manifest); !status) {
                result.diagnostics.push_back(
                    modding::ModDiagnostic{modding::DiagnosticSeverity::error, manifest_path,
                                           status.error().code, status.error().message});
            }
            result.packs.push_back(std::move(manifest));
        }
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
