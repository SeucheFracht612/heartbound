#pragma once

#include "engine/modding/generic_prototype.hpp"
#include "engine/modding/mod_diagnostic.hpp"
#include "engine/modding/prototype_registry.hpp"

#include <vector>

namespace heartstead::modding {

struct PrototypeSemanticValidationResult {
    std::vector<ModDiagnostic> diagnostics;

    [[nodiscard]] bool has_errors() const noexcept;
};

class PrototypeSemanticValidator {
  public:
    [[nodiscard]] static PrototypeSemanticValidationResult
    validate(const std::vector<GenericPrototype>& prototypes, const PrototypeRegistry& registry);
};

} // namespace heartstead::modding
