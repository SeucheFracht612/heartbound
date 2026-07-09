#pragma once

#include "engine/core/ids.hpp"

namespace heartstead::save {

using SaveId = core::SaveId;
using RuntimeHandle = core::RuntimeHandle;
using PrototypeId = core::PrototypeId;

struct SaveIdentity {
    SaveId save_id;
    PrototypeId prototype_id;

    [[nodiscard]] bool is_persistent() const noexcept {
        return save_id.is_valid() && prototype_id.is_valid();
    }
};

} // namespace heartstead::save
