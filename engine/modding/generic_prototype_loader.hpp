#pragma once

#include "engine/modding/generic_prototype.hpp"
#include "engine/modding/mod_diagnostic.hpp"
#include "engine/modding/mod_manifest.hpp"

#include <vector>

namespace heartstead::modding {

struct GenericPrototypeLoadResult {
    std::vector<GenericPrototype> prototypes;
    std::vector<GenericPrototypePatch> prototype_patches;
    std::size_t applied_patch_count = 0;
    std::vector<ModDiagnostic> diagnostics;

    [[nodiscard]] bool has_errors() const noexcept;
};

class GenericPrototypeLoader {
  public:
    [[nodiscard]] GenericPrototypeLoadResult
    load_from_mods(const std::vector<ModManifest>& mods) const;
};

} // namespace heartstead::modding
