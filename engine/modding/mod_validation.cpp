#include "engine/modding/mod_validation.hpp"

#include "engine/modding/generic_prototype_loader.hpp"
#include "engine/modding/mod_discovery.hpp"
#include "engine/modding/mod_fingerprint.hpp"
#include "engine/modding/mod_lifecycle.hpp"
#include "engine/modding/prototype_semantic_validation.hpp"
#include "engine/scripting/script_module_loader.hpp"

#include <algorithm>
#include <utility>

namespace heartstead::modding {

namespace {

void append_diagnostics(std::vector<ModDiagnostic>& destination,
                        std::vector<ModDiagnostic> diagnostics) {
    destination.insert(destination.end(), std::make_move_iterator(diagnostics.begin()),
                       std::make_move_iterator(diagnostics.end()));
}

} // namespace

bool ModValidationReport::has_errors() const noexcept {
    return std::ranges::any_of(diagnostics, [](const ModDiagnostic& diagnostic) {
        return diagnostic.severity == DiagnosticSeverity::error;
    });
}

std::size_t ModValidationReport::count_severity(DiagnosticSeverity severity) const noexcept {
    return static_cast<std::size_t>(
        std::ranges::count_if(diagnostics, [severity](const ModDiagnostic& diagnostic) {
            return diagnostic.severity == severity;
        }));
}

std::size_t ModValidationReport::count_kind(std::string_view kind) const noexcept {
    return static_cast<std::size_t>(std::ranges::count_if(
        prototypes, [kind](const GenericPrototype& prototype) { return prototype.kind == kind; }));
}

ModValidationReport ModValidation::validate(const std::filesystem::path& mods_root) {
    ModValidationReport report;

    ModDiscoverer discoverer;
    auto discovery = discoverer.discover(mods_root);
    report.mods = std::move(discovery.mods);
    append_diagnostics(report.diagnostics, std::move(discovery.diagnostics));

    report.lifecycle_plan = ModLifecyclePlanner::build(report.mods);
    append_diagnostics(report.diagnostics, report.lifecycle_plan.diagnostics);

    auto script_modules =
        scripting::ScriptModuleLoader::load_from_plan(report.mods, report.lifecycle_plan);
    report.script_modules = std::move(script_modules.modules);
    append_diagnostics(report.diagnostics, std::move(script_modules.diagnostics));

    GenericPrototypeLoader loader;
    auto loaded = loader.load_from_mods(report.mods);
    report.prototypes = std::move(loaded.prototypes);
    report.prototype_patches = std::move(loaded.prototype_patches);
    report.applied_patch_count = loaded.applied_patch_count;
    append_diagnostics(report.diagnostics, std::move(loaded.diagnostics));
    report.mod_fingerprints =
        build_mod_prototype_fingerprints(report.mods, report.prototypes, report.prototype_patches);

    auto registry_build = report.registry.build(report.prototypes);
    append_diagnostics(report.diagnostics, std::move(registry_build.diagnostics));

    auto semantic_validation =
        PrototypeSemanticValidator::validate(report.prototypes, report.registry);
    append_diagnostics(report.diagnostics, std::move(semantic_validation.diagnostics));

    return report;
}

std::string_view diagnostic_severity_name(DiagnosticSeverity severity) noexcept {
    switch (severity) {
    case DiagnosticSeverity::info:
        return "info";
    case DiagnosticSeverity::warning:
        return "warning";
    case DiagnosticSeverity::error:
        return "error";
    }
    return "unknown";
}

} // namespace heartstead::modding
