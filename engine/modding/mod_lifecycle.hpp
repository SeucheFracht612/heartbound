#pragma once

#include "engine/modding/mod_diagnostic.hpp"
#include "engine/modding/mod_manifest.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace heartstead::modding {

enum class ModLifecycleStage {
    settings,
    prototypes,
    data_updates,
    final_fixes,
    assets,
    migration,
    runtime_server,
    runtime_client,
};

enum class ModLifecycleTaskKind {
    manifest,
    prototype_definition,
    prototype_patch,
    final_patch,
    asset_file,
    resource_file,
    migration_script,
    runtime_server_script,
    runtime_client_script,
};

struct ModLifecycleTask {
    ModLifecycleStage stage = ModLifecycleStage::settings;
    ModLifecycleTaskKind kind = ModLifecycleTaskKind::manifest;
    std::string mod_id;
    std::filesystem::path source;
};

struct ModLifecyclePlan {
    std::vector<ModLifecycleTask> tasks;
    std::vector<ModDiagnostic> diagnostics;

    [[nodiscard]] bool has_errors() const noexcept;
    [[nodiscard]] std::size_t count_stage(ModLifecycleStage stage) const noexcept;
    [[nodiscard]] std::vector<ModLifecycleTask> tasks_for_stage(ModLifecycleStage stage) const;
};

class ModLifecyclePlanner {
  public:
    [[nodiscard]] static ModLifecyclePlan build(const std::vector<ModManifest>& mods);
};

[[nodiscard]] std::string_view mod_lifecycle_stage_name(ModLifecycleStage stage) noexcept;
[[nodiscard]] std::string_view mod_lifecycle_task_kind_name(ModLifecycleTaskKind kind) noexcept;

} // namespace heartstead::modding
