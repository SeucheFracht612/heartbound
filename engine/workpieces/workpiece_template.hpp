#pragma once

#include "engine/core/result.hpp"
#include "engine/workpieces/workpiece_grid.hpp"

#include <string>
#include <vector>

namespace heartstead::workpieces {

struct WorkpieceTemplateCell {
    WorkpieceCellCoord coord;
    WorkpieceCell cell;
};

struct WorkpieceTemplate {
    std::string id;
    WorkpieceGridShape shape;
    std::vector<WorkpieceTemplateCell> required_cells;
    std::vector<WorkpieceCellCoord> forbidden_cells;
    bool strict = false;

    [[nodiscard]] core::Status validate_definition() const;
};

struct WorkpieceTemplateValidation {
    bool valid = false;
    std::vector<WorkpieceCellCoord> missing_required_cells;
    std::vector<WorkpieceCellCoord> mismatched_required_cells;
    std::vector<WorkpieceCellCoord> forbidden_occupied_cells;
    std::vector<WorkpieceCellCoord> unexpected_occupied_cells;
};

class WorkpieceTemplateMatcher {
  public:
    [[nodiscard]] static WorkpieceTemplateValidation match(const WorkpieceGrid& grid,
                                                           const WorkpieceTemplate& templ);
};

} // namespace heartstead::workpieces
