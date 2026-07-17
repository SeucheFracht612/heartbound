#include "engine/modding/generic_prototype_loader.hpp"

#include "engine/core/result.hpp"
#include "engine/modding/flat_manifest.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace heartstead::modding {

namespace {

[[nodiscard]] std::string required_value(const std::map<std::string, std::string>& values,
                                         std::string_view key, const std::filesystem::path& source,
                                         std::vector<ModDiagnostic>& diagnostics) {
    const auto found = values.find(std::string(key));
    if (found == values.end() || found->second.empty()) {
        diagnostics.push_back(ModDiagnostic{DiagnosticSeverity::error, source,
                                            "prototype.missing_field",
                                            "missing required field: " + std::string(key)});
        return {};
    }
    return found->second;
}

[[nodiscard]] bool starts_with(std::string_view value, std::string_view prefix) noexcept {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

[[nodiscard]] bool declares_dependency(const ModManifest& mod,
                                       std::string_view dependency_id) noexcept {
    return std::ranges::any_of(mod.dependencies, [dependency_id](const std::string& dependency) {
        return dependency == dependency_id;
    });
}

[[nodiscard]] core::Result<GenericPrototypePatch>
parse_patch(const std::filesystem::path& file, std::string_view source_mod_id,
            GenericPrototypePatchStage stage, std::vector<ModDiagnostic>& diagnostics) {
    const auto fields = parse_flat_manifest(file, diagnostics, {.diagnostic_prefix = "prototype"});
    const auto target_text = required_value(fields, "target", file, diagnostics);
    const auto target_id = core::PrototypeId::parse(target_text);
    if (!target_id) {
        return core::Result<GenericPrototypePatch>::failure(
            "prototype_patch.invalid_target", "prototype patch target must be namespace:local_id");
    }

    GenericPrototypePatch patch;
    patch.source_mod_id = source_mod_id;
    patch.stage = stage;
    patch.target_id = target_id.value();
    patch.source = file;

    for (const auto& [key, value] : fields) {
        if (key == "target") {
            continue;
        }
        if (!starts_with(key, "set.")) {
            return core::Result<GenericPrototypePatch>::failure(
                "prototype_patch.unknown_field",
                "prototype patch field must use set.<field>: " + key);
        }

        const auto patched_field = key.substr(4);
        if (patched_field.empty()) {
            return core::Result<GenericPrototypePatch>::failure(
                "prototype_patch.empty_field", "prototype patch set field must not be empty");
        }
        patch.set_fields.emplace(patched_field, value);
    }

    if (patch.set_fields.empty()) {
        return core::Result<GenericPrototypePatch>::failure(
            "prototype_patch.empty", "prototype patch must set at least one field");
    }

    return core::Result<GenericPrototypePatch>::success(std::move(patch));
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

void apply_patch(GenericPrototypeLoadResult& result,
                 std::unordered_map<std::string, std::size_t>& prototype_indexes,
                 const ModManifest& source_mod, const GenericPrototypePatch& patch) {
    const auto found = prototype_indexes.find(patch.target_id.value());
    if (found == prototype_indexes.end()) {
        add_error(result.diagnostics, patch.source, "prototype_patch.missing_target",
                  "prototype patch target does not exist: " + patch.target_id.value());
        return;
    }

    auto& prototype = result.prototypes[found->second];
    if (prototype.id.namespace_id() != source_mod.id &&
        !declares_dependency(source_mod, prototype.id.namespace_id())) {
        add_error(result.diagnostics, patch.source, "prototype_patch.missing_dependency",
                  "cross-mod prototype patch requires a dependency on target mod: " +
                      std::string(prototype.id.namespace_id()));
        return;
    }

    for (const auto& [field, value] : patch.set_fields) {
        if (field == "id" || field == "kind") {
            add_error(result.diagnostics, patch.source, "prototype_patch.immutable_field",
                      "prototype patch cannot modify immutable field: " + field);
            continue;
        }
        if (field == "display_name") {
            prototype.display_name = value;
        }
        prototype.fields[field] = value;
    }
    ++result.applied_patch_count;
}

void load_and_apply_patches(GenericPrototypeLoadResult& result,
                            std::unordered_map<std::string, std::size_t>& prototype_indexes,
                            const std::vector<ModManifest>& mods, std::string_view suffix,
                            GenericPrototypePatchStage stage) {
    for (const auto& mod : mods) {
        const auto data_root = mod.root / "data";
        if (!std::filesystem::is_directory(data_root)) {
            continue;
        }

        std::vector<std::filesystem::path> files;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(data_root)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".toml") {
                continue;
            }
            files.push_back(entry.path());
        }
        std::ranges::sort(files);

        for (const auto& file : files) {
            const auto filename = file.filename().string();
            if (!filename.ends_with(suffix)) {
                continue;
            }

            auto patch = parse_patch(file, mod.id, stage, result.diagnostics);
            if (!patch) {
                add_error(result.diagnostics, file, patch.error().code, patch.error().message);
                continue;
            }

            result.prototype_patches.push_back(std::move(patch).value());
            apply_patch(result, prototype_indexes, mod, result.prototype_patches.back());
        }
    }
}

} // namespace

std::string_view generic_prototype_patch_stage_name(GenericPrototypePatchStage stage) noexcept {
    switch (stage) {
    case GenericPrototypePatchStage::data_update:
        return "data_update";
    case GenericPrototypePatchStage::final_fix:
        return "final_fix";
    }
    return "unknown";
}

bool GenericPrototypeLoadResult::has_errors() const noexcept {
    return std::ranges::any_of(diagnostics, [](const ModDiagnostic& diagnostic) {
        return diagnostic.severity == DiagnosticSeverity::error;
    });
}

GenericPrototypeLoadResult
GenericPrototypeLoader::load_from_mods(const std::vector<ModManifest>& mods) const {
    GenericPrototypeLoadResult result;
    std::set<std::string> seen_ids;
    std::unordered_map<std::string, std::size_t> prototype_indexes;

    for (const auto& mod : mods) {
        const auto data_root = mod.root / "data";
        if (!std::filesystem::is_directory(data_root)) {
            continue;
        }

        std::vector<std::filesystem::path> files;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(data_root)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".toml") {
                continue;
            }
            files.push_back(entry.path());
        }
        std::ranges::sort(files);

        for (const auto& file : files) {
            const auto filename = file.filename().string();
            if (!filename.ends_with(".prototype.toml")) {
                continue;
            }

            auto fields =
                parse_flat_manifest(file, result.diagnostics, {.diagnostic_prefix = "prototype"});
            const auto kind = required_value(fields, "kind", file, result.diagnostics);
            const auto id_text = required_value(fields, "id", file, result.diagnostics);
            const auto display_name =
                required_value(fields, "display_name", file, result.diagnostics);

            const auto parsed_id = core::PrototypeId::parse(id_text);
            if (!parsed_id) {
                add_error(result.diagnostics, file, "prototype.invalid_id",
                          "prototype id must be namespace:local_id");
                continue;
            }

            if (parsed_id->namespace_id() != mod.id) {
                add_error(result.diagnostics, file, "prototype.namespace_mismatch",
                          "prototype namespace must match owning mod id for new definitions");
                continue;
            }

            if (!seen_ids.insert(parsed_id->value()).second) {
                add_error(result.diagnostics, file, "prototype.duplicate_id",
                          "duplicate prototype id: " + parsed_id->value());
                continue;
            }

            result.prototypes.push_back(GenericPrototype{
                kind,
                *parsed_id,
                display_name,
                file,
                std::unordered_map<std::string, std::string>(fields.begin(), fields.end()),
            });
            prototype_indexes.emplace(parsed_id->value(), result.prototypes.size() - 1);
        }
    }

    load_and_apply_patches(result, prototype_indexes, mods, ".prototype_patch.toml",
                           GenericPrototypePatchStage::data_update);
    load_and_apply_patches(result, prototype_indexes, mods, ".final_patch.toml",
                           GenericPrototypePatchStage::final_fix);

    std::ranges::sort(result.prototypes, {},
                      [](const GenericPrototype& prototype) { return prototype.id.value(); });
    return result;
}

} // namespace heartstead::modding
