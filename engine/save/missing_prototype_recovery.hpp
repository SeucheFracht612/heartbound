#pragma once

#include "engine/core/result.hpp"
#include "engine/modding/prototype_registry.hpp"
#include "engine/save/save_snapshot.hpp"

#include <cstddef>

namespace heartstead::save {

struct MissingPrototypeRecoveryReport {
    std::size_t placeholder_count = 0;
    std::size_t dependent_record_count = 0;
};

[[nodiscard]] core::Result<MissingPrototypeRecoveryReport>
preserve_missing_prototypes(SaveSnapshot& snapshot, const modding::PrototypeRegistry& prototypes);

} // namespace heartstead::save
