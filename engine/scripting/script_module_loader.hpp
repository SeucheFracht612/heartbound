#pragma once

#include "engine/modding/mod_diagnostic.hpp"
#include "engine/modding/mod_lifecycle.hpp"
#include "engine/modding/mod_manifest.hpp"
#include "engine/scripting/script_runtime.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

namespace heartstead::scripting {

struct ScriptModuleLoadConfig {
    std::uint32_t max_source_bytes = 256u * 1024u;
};

struct ScriptModuleLoadResult {
    std::vector<ScriptModuleDesc> modules;
    std::vector<modding::ModDiagnostic> diagnostics;

    [[nodiscard]] bool has_errors() const noexcept;
    [[nodiscard]] std::size_t count_stage(ScriptStage stage) const noexcept;
};

class ScriptModuleLoader {
  public:
    [[nodiscard]] static ScriptModuleLoadResult
    load_from_plan(const std::vector<modding::ModManifest>& mods,
                   const modding::ModLifecyclePlan& lifecycle_plan,
                   ScriptModuleLoadConfig config = {});
};

} // namespace heartstead::scripting
