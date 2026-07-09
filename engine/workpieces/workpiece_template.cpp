#include "engine/workpieces/workpiece_template.hpp"

#include <algorithm>
#include <set>

namespace heartstead::workpieces {

namespace {

[[nodiscard]] bool contains(WorkpieceGridShape shape, WorkpieceCellCoord coord) noexcept {
    return coord.x < shape.width && coord.y < shape.height && coord.z < shape.depth;
}

[[nodiscard]] bool is_valid_shape(WorkpieceGridShape shape) noexcept {
    return shape.width > 0 && shape.height > 0 && shape.depth > 0;
}

[[nodiscard]] bool is_required_coord(const WorkpieceTemplate& templ, WorkpieceCellCoord coord) {
    return std::ranges::any_of(templ.required_cells, [coord](const WorkpieceTemplateCell& cell) {
        return cell.coord == coord;
    });
}

} // namespace

core::Status WorkpieceTemplate::validate_definition() const {
    if (id.empty()) {
        return core::Status::failure("workpiece_template.missing_id",
                                     "workpiece template id is required");
    }
    if (!is_valid_shape(shape)) {
        return core::Status::failure("workpiece_template.invalid_shape",
                                     "workpiece template shape must be non-zero");
    }

    std::set<WorkpieceCellCoord> required_coords;
    for (const auto& required : required_cells) {
        if (!contains(shape, required.coord)) {
            return core::Status::failure("workpiece_template.required_out_of_bounds",
                                         "required cell is outside template shape");
        }
        if (!required.cell.is_occupied()) {
            return core::Status::failure("workpiece_template.empty_required_cell",
                                         "required cell must be occupied");
        }
        if (!required_coords.insert(required.coord).second) {
            return core::Status::failure("workpiece_template.duplicate_required_cell",
                                         "required cell coordinates must be unique");
        }
    }

    std::set<WorkpieceCellCoord> forbidden_coords;
    for (const auto& forbidden : forbidden_cells) {
        if (!contains(shape, forbidden)) {
            return core::Status::failure("workpiece_template.forbidden_out_of_bounds",
                                         "forbidden cell is outside template shape");
        }
        if (!forbidden_coords.insert(forbidden).second) {
            return core::Status::failure("workpiece_template.duplicate_forbidden_cell",
                                         "forbidden cell coordinates must be unique");
        }
        if (required_coords.contains(forbidden)) {
            return core::Status::failure("workpiece_template.conflicting_cell",
                                         "cell cannot be both required and forbidden");
        }
    }

    return core::Status::ok();
}

WorkpieceTemplateValidation WorkpieceTemplateMatcher::match(const WorkpieceGrid& grid,
                                                            const WorkpieceTemplate& templ) {
    WorkpieceTemplateValidation result;
    auto definition_status = templ.validate_definition();
    if (!definition_status || grid.shape() != templ.shape) {
        result.valid = false;
        return result;
    }

    for (const auto& required : templ.required_cells) {
        auto actual = grid.get(required.coord);
        if (!actual || !actual.value().is_occupied()) {
            result.missing_required_cells.push_back(required.coord);
            continue;
        }
        if (actual.value() != required.cell) {
            result.mismatched_required_cells.push_back(required.coord);
        }
    }

    for (const auto& forbidden : templ.forbidden_cells) {
        auto actual = grid.get(forbidden);
        if (actual && actual.value().is_occupied()) {
            result.forbidden_occupied_cells.push_back(forbidden);
        }
    }

    if (templ.strict) {
        const auto shape = grid.shape();
        for (std::uint16_t z = 0; z < shape.depth; ++z) {
            for (std::uint16_t y = 0; y < shape.height; ++y) {
                for (std::uint16_t x = 0; x < shape.width; ++x) {
                    const WorkpieceCellCoord coord{x, y, z};
                    auto actual = grid.get(coord);
                    if (actual && actual.value().is_occupied() &&
                        !is_required_coord(templ, coord)) {
                        result.unexpected_occupied_cells.push_back(coord);
                    }
                }
            }
        }
    }

    result.valid =
        result.missing_required_cells.empty() && result.mismatched_required_cells.empty() &&
        result.forbidden_occupied_cells.empty() && result.unexpected_occupied_cells.empty();
    return result;
}

} // namespace heartstead::workpieces
