#pragma once

#include "engine/modding/mod_diagnostic.hpp"
#include "engine/modding/mod_manifest.hpp"

#include <filesystem>
#include <vector>

namespace heartstead::modding {

struct ModDiscoveryResult {
    std::vector<ModManifest> mods;
    std::vector<ModDiagnostic> diagnostics;

    [[nodiscard]] bool has_errors() const noexcept;
};

class ModDiscoverer {
  public:
    [[nodiscard]] ModDiscoveryResult discover(const std::filesystem::path& mods_root) const;
};

} // namespace heartstead::modding
