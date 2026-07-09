#include "engine/workpieces/workpiece_definition.hpp"

namespace heartstead::workpieces {

core::Status WorkpieceDefinition::validate() const {
    if (!prototype_id.is_valid()) {
        return core::Status::failure("workpiece_definition.invalid_prototype",
                                     "workpiece definition prototype id must be valid");
    }
    if (!material_prototype_id.is_valid()) {
        return core::Status::failure("workpiece_definition.invalid_material",
                                     "workpiece definition material prototype id must be valid");
    }
    auto grid = WorkpieceGrid::create(grid_shape);
    if (!grid) {
        return core::Status::failure(grid.error().code, grid.error().message);
    }
    return core::Status::ok();
}

core::Result<WorkpieceGrid> WorkpieceDefinition::create_grid() const {
    auto status = validate();
    if (!status) {
        return core::Result<WorkpieceGrid>::failure(status.error().code, status.error().message);
    }
    return WorkpieceGrid::create(grid_shape);
}

} // namespace heartstead::workpieces
