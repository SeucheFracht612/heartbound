#include "engine/modding/mod_lifecycle.hpp"

#include "engine/core/filesystem.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <string_view>
#include <utility>

namespace heartstead::modding {

namespace {

constexpr std::array stage_order{
    ModLifecycleStage::settings,       ModLifecycleStage::prototypes,
    ModLifecycleStage::data_updates,   ModLifecycleStage::final_fixes,
    ModLifecycleStage::assets,         ModLifecycleStage::migration,
    ModLifecycleStage::runtime_server, ModLifecycleStage::runtime_client,
};

[[nodiscard]] std::size_t stage_sort_index(ModLifecycleStage stage) noexcept {
    for (std::size_t index = 0; index < stage_order.size(); ++index) {
        if (stage_order[index] == stage) {
            return index;
        }
    }
    return stage_order.size();
}

[[nodiscard]] bool path_has_extension(const std::filesystem::path& path,
                                      std::string_view extension) {
    return path.extension().generic_string() == extension;
}

[[nodiscard]] std::string filename_string(const std::filesystem::path& path) {
    return path.filename().generic_string();
}

[[nodiscard]] bool ends_with(std::string_view value, std::string_view suffix) noexcept {
    return value.size() >= suffix.size() && value.substr(value.size() - suffix.size()) == suffix;
}

void add_task(ModLifecyclePlan& plan, ModLifecycleStage stage, ModLifecycleTaskKind kind,
              const ModManifest& mod, std::filesystem::path source) {
    plan.tasks.push_back(ModLifecycleTask{stage, kind, mod.id, std::move(source)});
}

void add_diagnostic(ModLifecyclePlan& plan, DiagnosticSeverity severity,
                    std::filesystem::path source, std::string code, std::string message) {
    plan.diagnostics.push_back(ModDiagnostic{
        severity,
        std::move(source),
        std::move(code),
        std::move(message),
    });
}

void classify_file(ModLifecyclePlan& plan, const ModManifest& mod,
                   const std::filesystem::path& file) {
    auto relative_result = core::relative_path_below(mod.root, file);
    if (!relative_result) {
        add_diagnostic(plan, DiagnosticSeverity::error, file, "mod.lifecycle.unsafe_path",
                       relative_result.error().message);
        return;
    }
    const auto& relative = relative_result.value();
    const auto filename = filename_string(file);
    auto component = relative.begin();
    if (component == relative.end()) {
        add_diagnostic(plan, DiagnosticSeverity::error, file, "mod.lifecycle.unsafe_path",
                       "mod lifecycle file has no relative path below its mod root");
        return;
    }
    const auto top_level = component->generic_string();
    const auto is_lua_source =
        path_has_extension(file, ".lua") || path_has_extension(file, ".luau");

    if (top_level != "scripts" && top_level != "migrations" && is_lua_source) {
        add_diagnostic(plan, DiagnosticSeverity::error, file, "mod.lifecycle.script_outside_stage",
                       "Lua script files must live in a declared runtime or migration stage");
        return;
    }

    if (top_level == "data") {
        if (ends_with(filename, ".prototype.toml")) {
            add_task(plan, ModLifecycleStage::prototypes,
                     ModLifecycleTaskKind::prototype_definition, mod, file);
            return;
        }
        if (ends_with(filename, ".prototype_patch.toml")) {
            add_task(plan, ModLifecycleStage::data_updates, ModLifecycleTaskKind::prototype_patch,
                     mod, file);
            return;
        }
        if (ends_with(filename, ".final_patch.toml")) {
            add_task(plan, ModLifecycleStage::final_fixes, ModLifecycleTaskKind::final_patch, mod,
                     file);
            return;
        }
    }

    if (top_level == "assets" || top_level == "locale") {
        add_task(plan, ModLifecycleStage::assets,
                 top_level == "locale" ? ModLifecycleTaskKind::resource_file
                                       : ModLifecycleTaskKind::asset_file,
                 mod, file);
        return;
    }

    if (top_level == "migrations") {
        add_task(plan, ModLifecycleStage::migration, ModLifecycleTaskKind::migration_script, mod,
                 file);
        return;
    }

    if (top_level != "scripts") {
        return;
    }

    if (!is_lua_source) {
        add_diagnostic(plan, DiagnosticSeverity::warning, file, "mod.lifecycle.unknown_script_file",
                       "script directory contains a non-Lua script file");
        return;
    }

    ++component;
    const auto script_stage =
        component == relative.end() ? std::string{} : component->generic_string();
    if (script_stage == "runtime_server") {
        add_task(plan, ModLifecycleStage::runtime_server,
                 ModLifecycleTaskKind::runtime_server_script, mod, file);
        return;
    }
    if (script_stage == "runtime_client") {
        add_task(plan, ModLifecycleStage::runtime_client,
                 ModLifecycleTaskKind::runtime_client_script, mod, file);
        return;
    }
    if (script_stage == "migration" || script_stage == "migrations") {
        add_task(plan, ModLifecycleStage::migration, ModLifecycleTaskKind::migration_script, mod,
                 file);
        return;
    }

    add_diagnostic(plan, DiagnosticSeverity::error, file, "mod.lifecycle.unknown_script_stage",
                   "script file must live under scripts/runtime_server, scripts/runtime_client, "
                   "scripts/migration, or scripts/migrations");
}

} // namespace

bool ModLifecyclePlan::has_errors() const noexcept {
    return std::ranges::any_of(diagnostics, [](const ModDiagnostic& diagnostic) {
        return diagnostic.severity == DiagnosticSeverity::error;
    });
}

std::size_t ModLifecyclePlan::count_stage(ModLifecycleStage stage) const noexcept {
    return static_cast<std::size_t>(std::ranges::count_if(
        tasks, [stage](const ModLifecycleTask& task) { return task.stage == stage; }));
}

std::vector<ModLifecycleTask> ModLifecyclePlan::tasks_for_stage(ModLifecycleStage stage) const {
    std::vector<ModLifecycleTask> result;
    for (const auto& task : tasks) {
        if (task.stage == stage) {
            result.push_back(task);
        }
    }
    return result;
}

ModLifecyclePlan ModLifecyclePlanner::build(const std::vector<ModManifest>& mods) {
    ModLifecyclePlan plan;

    for (const auto& mod : mods) {
        add_task(plan, ModLifecycleStage::settings, ModLifecycleTaskKind::manifest, mod,
                 mod.root / "mod.toml");

        std::error_code error;
        const auto root_status = std::filesystem::symlink_status(mod.root, error);
        if (error || !std::filesystem::is_directory(root_status)) {
            add_diagnostic(plan, DiagnosticSeverity::error, mod.root, "mod.lifecycle.root_missing",
                           "mod lifecycle root is not a non-symlink directory");
            continue;
        }

        auto files = core::list_regular_files_recursive(mod.root);
        if (!files) {
            add_diagnostic(plan, DiagnosticSeverity::error, mod.root, files.error().code,
                           files.error().message);
            continue;
        }

        for (const auto& file : files.value()) {
            if (file.filename() == "mod.toml") {
                continue;
            }
            classify_file(plan, mod, file);
        }
    }

    std::ranges::stable_sort(plan.tasks,
                             [](const ModLifecycleTask& left, const ModLifecycleTask& right) {
                                 const auto left_stage = stage_sort_index(left.stage);
                                 const auto right_stage = stage_sort_index(right.stage);
                                 return left_stage < right_stage;
                             });

    return plan;
}

std::string_view mod_lifecycle_stage_name(ModLifecycleStage stage) noexcept {
    switch (stage) {
    case ModLifecycleStage::settings:
        return "settings";
    case ModLifecycleStage::prototypes:
        return "prototypes";
    case ModLifecycleStage::data_updates:
        return "data_updates";
    case ModLifecycleStage::final_fixes:
        return "final_fixes";
    case ModLifecycleStage::assets:
        return "assets";
    case ModLifecycleStage::migration:
        return "migration";
    case ModLifecycleStage::runtime_server:
        return "runtime_server";
    case ModLifecycleStage::runtime_client:
        return "runtime_client";
    }
    return "unknown";
}

std::string_view mod_lifecycle_task_kind_name(ModLifecycleTaskKind kind) noexcept {
    switch (kind) {
    case ModLifecycleTaskKind::manifest:
        return "manifest";
    case ModLifecycleTaskKind::prototype_definition:
        return "prototype_definition";
    case ModLifecycleTaskKind::prototype_patch:
        return "prototype_patch";
    case ModLifecycleTaskKind::final_patch:
        return "final_patch";
    case ModLifecycleTaskKind::asset_file:
        return "asset_file";
    case ModLifecycleTaskKind::resource_file:
        return "resource_file";
    case ModLifecycleTaskKind::migration_script:
        return "migration_script";
    case ModLifecycleTaskKind::runtime_server_script:
        return "runtime_server_script";
    case ModLifecycleTaskKind::runtime_client_script:
        return "runtime_client_script";
    }
    return "unknown";
}

} // namespace heartstead::modding
