#include "engine/content/content_validation.hpp"
#include "engine/core/logging.hpp"
#include "engine/core/process_entry.hpp"
#include "engine/debug/inspection.hpp"
#include "engine/modding/mod_validation.hpp"
#include "engine/modding/prototype_registry.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace {

struct CliOptions {
    std::filesystem::path source_root;
    bool inspect = false;
};

enum class CliParseResult {
    ok,
    help,
    error,
};

void print_usage(std::ostream& output) {
    output << "usage: heartstead_mod_validator [source_root] [--inspect]\n";
}

CliParseResult parse_cli(int argc, char** argv, CliOptions& options) {
    std::size_t positional_count = 0;
    for (int index = 1; index < argc; ++index) {
        const std::string_view arg = argv[index];
        if (arg == "--help" || arg == "-h") {
            return CliParseResult::help;
        }
        if (arg == "--inspect") {
            options.inspect = true;
            continue;
        }
        if (arg.starts_with("--")) {
            return CliParseResult::error;
        }

        ++positional_count;
        if (positional_count > 1) {
            return CliParseResult::error;
        }
        options.source_root = std::filesystem::path(argv[index]);
    }
    return CliParseResult::ok;
}

void log_diagnostic(const heartstead::modding::ModDiagnostic& diagnostic) {
    const auto severity = heartstead::modding::diagnostic_severity_name(diagnostic.severity);
    const auto level = diagnostic.severity == heartstead::modding::DiagnosticSeverity::error
                           ? heartstead::core::LogLevel::error
                       : diagnostic.severity == heartstead::modding::DiagnosticSeverity::warning
                           ? heartstead::core::LogLevel::warning
                           : heartstead::core::LogLevel::info;

    heartstead::core::log(level, std::string(severity) + ':' + diagnostic.code + ": " +
                                     diagnostic.message + " (" +
                                     diagnostic.source.generic_string() + ")");
}

void log_kind_count(const heartstead::content::ContentValidationReport& report,
                    std::string_view kind) {
    heartstead::core::log(heartstead::core::LogLevel::info,
                          std::string(kind) +
                              " prototypes: " + std::to_string(report.count_kind(kind)));
}

std::string dependency_summary(const heartstead::modding::ModManifest& mod) {
    if (mod.dependencies.empty()) {
        return "none";
    }

    std::string output;
    for (std::size_t index = 0; index < mod.dependencies.size(); ++index) {
        if (index > 0) {
            output += ',';
        }
        output += mod.dependencies[index];
    }
    return output;
}

void log_lifecycle_plan(const heartstead::modding::ModLifecyclePlan& plan) {
    using heartstead::core::LogLevel;
    using heartstead::modding::ModLifecycleStage;

    constexpr std::array stages{
        ModLifecycleStage::settings,       ModLifecycleStage::prototypes,
        ModLifecycleStage::data_updates,   ModLifecycleStage::final_fixes,
        ModLifecycleStage::assets,         ModLifecycleStage::migration,
        ModLifecycleStage::runtime_server, ModLifecycleStage::runtime_client,
    };

    heartstead::core::log(LogLevel::info,
                          "Mod lifecycle tasks: " + std::to_string(plan.tasks.size()));
    for (const auto stage : stages) {
        heartstead::core::log(
            LogLevel::info, "  stage " +
                                std::string(heartstead::modding::mod_lifecycle_stage_name(stage)) +
                                " tasks=" + std::to_string(plan.count_stage(stage)));
    }

    for (const auto& task : plan.tasks) {
        heartstead::core::log(
            LogLevel::info,
            "  lifecycle " +
                std::string(heartstead::modding::mod_lifecycle_stage_name(task.stage)) + " " +
                std::string(heartstead::modding::mod_lifecycle_task_kind_name(task.kind)) +
                " mod=" + task.mod_id + " source=" + task.source.generic_string());
    }
}

std::size_t count_patch_stage(const heartstead::content::ContentValidationReport& report,
                              heartstead::modding::GenericPrototypePatchStage stage) {
    return static_cast<std::size_t>(std::ranges::count_if(
        report.prototype_patches, [stage](const heartstead::modding::GenericPrototypePatch& patch) {
            return patch.stage == stage;
        }));
}

std::size_t count_script_stage(const heartstead::content::ContentValidationReport& report,
                               heartstead::scripting::ScriptStage stage) {
    return static_cast<std::size_t>(std::ranges::count_if(
        report.script_modules, [stage](const heartstead::scripting::ScriptModuleDesc& module) {
            return module.stage == stage;
        }));
}

std::string permission_summary(const heartstead::scripting::ScriptModuleDesc& module) {
    if (module.permissions.empty()) {
        return "none";
    }

    std::string output;
    for (std::size_t index = 0; index < module.permissions.size(); ++index) {
        if (index > 0) {
            output += ',';
        }
        output += heartstead::scripting::script_permission_name(module.permissions[index]);
    }
    return output;
}

} // namespace

int main(int argc, char** argv) {
    return heartstead::core::run_process_entry(argv[0], [argc, argv] {
        using namespace heartstead;

        CliOptions cli{std::filesystem::path(HEARTSTEAD_SOURCE_ROOT)};
        switch (parse_cli(argc, argv, cli)) {
        case CliParseResult::ok:
            break;
        case CliParseResult::help:
            print_usage(std::cout);
            return 0;
        case CliParseResult::error:
            print_usage(std::cerr);
            return 2;
        }

        auto report = content::ContentValidation::validate(cli.source_root);
        for (const auto& diagnostic : report.diagnostics) {
            log_diagnostic(diagnostic);
        }

        core::log(core::LogLevel::info, "Mods: " + std::to_string(report.mods.size()));
        for (const auto& mod : report.mods) {
            core::log(core::LogLevel::info, "  mod " + mod.id + " version=" + mod.version +
                                                " dependencies=" + dependency_summary(mod));
        }
        core::log(core::LogLevel::info,
                  "Mod prototype fingerprints: " + std::to_string(report.mod_fingerprints.size()));
        for (const auto& fingerprint : report.mod_fingerprints) {
            core::log(core::LogLevel::info,
                      "  " + fingerprint.id +
                          " prototypes=" + std::to_string(fingerprint.prototype_count) +
                          " patches=" + std::to_string(fingerprint.patch_count) +
                          " hash=" + fingerprint.prototype_hash);
        }
        core::log(core::LogLevel::info,
                  "Prototype patches: " + std::to_string(report.prototype_patches.size()) +
                      ", applied=" + std::to_string(report.applied_patch_count));
        core::log(core::LogLevel::info,
                  "  data-update patches=" +
                      std::to_string(count_patch_stage(
                          report, modding::GenericPrototypePatchStage::data_update)) +
                      ", final-fix patches=" +
                      std::to_string(count_patch_stage(
                          report, modding::GenericPrototypePatchStage::final_fix)));
        log_lifecycle_plan(report.lifecycle_plan);
        core::log(
            core::LogLevel::info,
            "Script modules: " + std::to_string(report.script_modules.size()) + " runtime_server=" +
                std::to_string(count_script_stage(report, scripting::ScriptStage::runtime_server)) +
                " runtime_client=" +
                std::to_string(count_script_stage(report, scripting::ScriptStage::runtime_client)) +
                " migration=" +
                std::to_string(count_script_stage(report, scripting::ScriptStage::migration)));
        for (const auto& module : report.script_modules) {
            core::log(core::LogLevel::info,
                      "  script " + module.module_id +
                          " stage=" + std::string(scripting::script_stage_name(module.stage)) +
                          " permissions=" + permission_summary(module) +
                          " source=" + module.source_path.generic_string());
        }
        core::log(core::LogLevel::info,
                  "Resource packs: " + std::to_string(report.resource_packs.size()));
        core::log(core::LogLevel::info, "Prototypes: " + std::to_string(report.prototypes.size()));
        core::log(core::LogLevel::info,
                  "Active assets: " + std::to_string(report.asset_catalog.active_count()));
        core::log(core::LogLevel::info,
                  "Item definitions: " + std::to_string(report.item_definitions.size()));
        core::log(core::LogLevel::info,
                  "Cargo definitions: " + std::to_string(report.cargo_definitions.size()));
        core::log(core::LogLevel::info,
                  "Entity definitions: " + std::to_string(report.entity_definitions.size()));
        core::log(core::LogLevel::info,
                  "Voxel palette entries: " + std::to_string(report.voxel_palette.size()));
        core::log(core::LogLevel::info,
                  "Assembly definitions: " + std::to_string(report.assembly_definitions.size()));
        core::log(core::LogLevel::info,
                  "Process definitions: " + std::to_string(report.process_definitions.size()));
        core::log(core::LogLevel::info,
                  "Room descriptor definitions: " +
                      std::to_string(report.room_descriptor_definitions.size()));
        core::log(core::LogLevel::info,
                  "Workpiece definitions: " + std::to_string(report.workpiece_definitions.size()));
        core::log(core::LogLevel::info,
                  "Scenario definitions: " + std::to_string(report.scenario_definitions.size()));
        core::log(core::LogLevel::info,
                  "Material definitions: " + std::to_string(report.material_registry.size()));
        core::log(core::LogLevel::info,
                  "Material asset references: " +
                      std::to_string(report.material_assets.references.size()) +
                      ", overrides=" + std::to_string(report.material_assets.override_count()));
        core::log(core::LogLevel::info, "Warnings: " + std::to_string(report.count_severity(
                                                           modding::DiagnosticSeverity::warning)));
        core::log(core::LogLevel::info, "Errors: " + std::to_string(report.count_severity(
                                                         modding::DiagnosticSeverity::error)));

        log_kind_count(report, modding::PrototypeKinds::item);
        log_kind_count(report, modding::PrototypeKinds::cargo);
        log_kind_count(report, modding::PrototypeKinds::entity);
        log_kind_count(report, modding::PrototypeKinds::voxel);
        log_kind_count(report, modding::PrototypeKinds::block_model);
        log_kind_count(report, modding::PrototypeKinds::build_piece);
        log_kind_count(report, modding::PrototypeKinds::assembly);
        log_kind_count(report, modding::PrototypeKinds::workpiece);
        log_kind_count(report, modding::PrototypeKinds::pattern);
        log_kind_count(report, modding::PrototypeKinds::process);
        log_kind_count(report, modding::PrototypeKinds::fire);
        log_kind_count(report, modding::PrototypeKinds::room_descriptor);
        log_kind_count(report, modding::PrototypeKinds::material);
        log_kind_count(report, modding::PrototypeKinds::scenario);

        if (cli.inspect) {
            std::cout << debug::Inspector::render_text(debug::Inspector::inspect(report));
        }

        return report.has_errors() ? 1 : 0;
    });
}
