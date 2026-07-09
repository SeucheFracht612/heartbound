#pragma once

#include "engine/modding/generic_prototype.hpp"
#include "engine/modding/mod_diagnostic.hpp"
#include "engine/modding/mod_fingerprint.hpp"
#include "engine/modding/mod_lifecycle.hpp"
#include "engine/modding/mod_manifest.hpp"
#include "engine/modding/prototype_registry.hpp"
#include "engine/scripting/script_runtime.hpp"

#include <filesystem>
#include <string_view>
#include <vector>

namespace heartstead::modding {

struct ModValidationReport {
    std::vector<ModManifest> mods;
    std::vector<GenericPrototype> prototypes;
    std::vector<GenericPrototypePatch> prototype_patches;
    std::size_t applied_patch_count = 0;
    std::vector<ModPrototypeFingerprint> mod_fingerprints;
    ModLifecyclePlan lifecycle_plan;
    std::vector<scripting::ScriptModuleDesc> script_modules;
    PrototypeRegistry registry;
    std::vector<ModDiagnostic> diagnostics;

    [[nodiscard]] bool has_errors() const noexcept;
    [[nodiscard]] std::size_t count_severity(DiagnosticSeverity severity) const noexcept;
    [[nodiscard]] std::size_t count_kind(std::string_view kind) const noexcept;
};

class ModValidation {
  public:
    [[nodiscard]] static ModValidationReport validate(const std::filesystem::path& mods_root);
};

[[nodiscard]] std::string_view diagnostic_severity_name(DiagnosticSeverity severity) noexcept;

} // namespace heartstead::modding
