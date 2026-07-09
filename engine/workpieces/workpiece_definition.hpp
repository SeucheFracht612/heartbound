#pragma once

#include "engine/core/ids.hpp"
#include "engine/core/result.hpp"
#include "engine/workpieces/workpiece_grid.hpp"

namespace heartstead::workpieces {

struct WorkpieceDefinition {
    core::PrototypeId prototype_id;
    core::PrototypeId material_prototype_id;
    WorkpieceGridShape grid_shape;

    [[nodiscard]] core::Status validate() const;
    [[nodiscard]] core::Result<WorkpieceGrid> create_grid() const;
};

} // namespace heartstead::workpieces
