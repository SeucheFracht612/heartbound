#include "engine/assets/resource_pack.hpp"

#include "engine/core/ids.hpp"

#include <algorithm>
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
