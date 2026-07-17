#include "engine/modding/mod_discovery.hpp"

#include "engine/core/ids.hpp"

#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace heartstead::modding {

namespace {

[[nodiscard]] std::string trim(std::string_view value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return std::string(value.substr(first, last - first + 1));
}

[[nodiscard]] std::string unquote(std::string value) {
    value = trim(value);
    if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') ||
                              (value.front() == '\'' && value.back() == '\''))) {
        return value.substr(1, value.size() - 2);
    }
    return value;
}

[[nodiscard]] std::map<std::string, std::string>
parse_flat_toml(const std::filesystem::path& file, std::vector<ModDiagnostic>& diagnostics) {
    std::ifstream input(file);
    if (!input) {
        diagnostics.push_back(ModDiagnostic{DiagnosticSeverity::error, file,
                                            "mod.manifest.unreadable",
                                            "could not read mod manifest"});
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
            diagnostics.push_back(ModDiagnostic{
                DiagnosticSeverity::error,
                file,
                "mod.manifest.syntax",
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
                                         std::vector<ModDiagnostic>& diagnostics) {
    const auto found = values.find(std::string(key));
    if (found == values.end() || found->second.empty()) {
        diagnostics.push_back(ModDiagnostic{DiagnosticSeverity::error, source,
                                            "mod.manifest.missing_field",
                                            "missing required field: " + std::string(key)});
        return {};
    }
    return found->second;
}

[[nodiscard]] std::vector<std::string> split_csv(std::string_view value) {
    std::vector<std::string> result;
    std::size_t start = 0;
    while (start <= value.size()) {
        const auto end = value.find(',', start);
        const auto token =
            end == std::string_view::npos ? value.substr(start) : value.substr(start, end - start);
        auto trimmed = trim(std::string(token));
        if (!trimmed.empty()) {
            result.push_back(std::move(trimmed));
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    return result;
}

void add_error(std::vector<ModDiagnostic>& diagnostics, std::filesystem::path source,
               std::string code, std::string message) {
    diagnostics.push_back(ModDiagnostic{
        DiagnosticSeverity::error,
        std::move(source),
        std::move(code),
        std::move(message),
    });
}

void validate_dependencies(ModDiscoveryResult& result) {
    std::unordered_map<std::string, std::size_t> by_id;
    for (std::size_t index = 0; index < result.mods.size(); ++index) {
        by_id.emplace(result.mods[index].id, index);
    }

    for (const auto& mod : result.mods) {
        std::set<std::string> seen_dependencies;
        for (const auto& dependency : mod.dependencies) {
            const auto source = mod.root / "mod.toml";
            if (!core::is_valid_namespace_id(dependency)) {
                add_error(result.diagnostics, source, "mod.manifest.invalid_dependency",
                          "mod dependency id must be a valid namespace id: " + dependency);
                continue;
            }
            if (!seen_dependencies.insert(dependency).second) {
                add_error(result.diagnostics, source, "mod.manifest.duplicate_dependency",
                          "mod manifest repeats dependency: " + dependency);
            }
            if (dependency == mod.id) {
                add_error(result.diagnostics, source, "mod.manifest.self_dependency",
                          "mod cannot depend on itself: " + mod.id);
                continue;
            }
            if (!by_id.contains(dependency)) {
                add_error(result.diagnostics, source, "mod.manifest.missing_dependency",
                          "mod dependency is not loaded: " + dependency);
            }
        }
    }
}

void sort_by_dependencies(ModDiscoveryResult& result) {
    std::ranges::sort(result.mods, {}, &ModManifest::id);

    std::unordered_map<std::string, std::size_t> by_id;
    for (std::size_t index = 0; index < result.mods.size(); ++index) {
        by_id.emplace(result.mods[index].id, index);
    }

    std::vector<std::vector<std::size_t>> dependents(result.mods.size());
    std::vector<std::size_t> indegree(result.mods.size(), 0);
    for (std::size_t index = 0; index < result.mods.size(); ++index) {
        std::set<std::string> seen_dependencies;
        for (const auto& dependency : result.mods[index].dependencies) {
            if (!seen_dependencies.insert(dependency).second ||
                dependency == result.mods[index].id) {
                continue;
            }
            const auto found = by_id.find(dependency);
            if (found == by_id.end()) {
                continue;
            }
            dependents[found->second].push_back(index);
            ++indegree[index];
        }
    }

    std::vector<std::size_t> ready;
    for (std::size_t index = 0; index < result.mods.size(); ++index) {
        if (indegree[index] == 0) {
            ready.push_back(index);
        }
    }

    std::vector<ModManifest> ordered;
    ordered.reserve(result.mods.size());
    while (!ready.empty()) {
        std::ranges::sort(ready, {},
                          [&result](std::size_t index) { return result.mods[index].id; });
        const auto index = ready.front();
        ready.erase(ready.begin());
        ordered.push_back(result.mods[index]);

        for (const auto dependent : dependents[index]) {
            --indegree[dependent];
            if (indegree[dependent] == 0) {
                ready.push_back(dependent);
            }
        }
    }

    if (ordered.size() != result.mods.size()) {
        add_error(result.diagnostics, {}, "mod.manifest.dependency_cycle",
                  "mod dependency graph contains a cycle");
        std::ranges::sort(result.mods, {}, &ModManifest::id);
        return;
    }

    result.mods = std::move(ordered);
}

} // namespace

bool ModDiscoveryResult::has_errors() const noexcept {
    return std::ranges::any_of(diagnostics, [](const ModDiagnostic& diagnostic) {
        return diagnostic.severity == DiagnosticSeverity::error;
    });
}

ModDiscoveryResult ModDiscoverer::discover(const std::filesystem::path& mods_root) const {
    ModDiscoveryResult result;

    if (!std::filesystem::is_directory(mods_root)) {
        result.diagnostics.push_back(ModDiagnostic{DiagnosticSeverity::error, mods_root,
                                                   "mod.root.missing", "mods root does not exist"});
        return result;
    }

    std::set<std::string> seen_ids;
    for (const auto& entry : std::filesystem::directory_iterator(mods_root)) {
        if (!entry.is_directory()) {
            continue;
        }

        const auto manifest_path = entry.path() / "mod.toml";
        if (!std::filesystem::exists(manifest_path)) {
            result.diagnostics.push_back(ModDiagnostic{DiagnosticSeverity::warning, entry.path(),
                                                       "mod.manifest.missing",
                                                       "directory does not contain mod.toml"});
            continue;
        }

        const auto values = parse_flat_toml(manifest_path, result.diagnostics);
        ModManifest manifest;
        manifest.id = required_value(values, "id", manifest_path, result.diagnostics);
        manifest.name = required_value(values, "name", manifest_path, result.diagnostics);
        manifest.version = required_value(values, "version", manifest_path, result.diagnostics);
        manifest.description = values.contains("description") ? values.at("description") : "";
        manifest.dependencies = values.contains("dependencies")
                                    ? split_csv(values.at("dependencies"))
                                    : std::vector<std::string>{};
        manifest.root = entry.path();

        if (!core::is_valid_namespace_id(manifest.id)) {
            result.diagnostics.push_back(ModDiagnostic{DiagnosticSeverity::error, manifest_path,
                                                       "mod.manifest.invalid_id",
                                                       "mod id must be a valid namespace id"});
        }

        if (!manifest.id.empty() && !seen_ids.insert(manifest.id).second) {
            result.diagnostics.push_back(ModDiagnostic{DiagnosticSeverity::error, manifest_path,
                                                       "mod.manifest.duplicate_id",
                                                       "duplicate mod id: " + manifest.id});
        }

        if (!manifest.id.empty()) {
            result.mods.push_back(std::move(manifest));
        }
    }

    validate_dependencies(result);
    sort_by_dependencies(result);
    return result;
}

} // namespace heartstead::modding
