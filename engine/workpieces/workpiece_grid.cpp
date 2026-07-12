#include "engine/workpieces/workpiece_grid.hpp"

#include <limits>

namespace heartstead::workpieces {

namespace {

constexpr std::uint16_t max_axis = 64;

[[nodiscard]] bool is_valid_shape(WorkpieceGridShape shape) noexcept {
    return shape.width > 0 && shape.height > 0 && shape.depth > 0 && shape.width <= max_axis &&
           shape.height <= max_axis && shape.depth <= max_axis;
}

[[nodiscard]] std::size_t cell_count(WorkpieceGridShape shape) noexcept {
    return static_cast<std::size_t>(shape.width) * static_cast<std::size_t>(shape.height) *
           static_cast<std::size_t>(shape.depth);
}

} // namespace

core::Result<WorkpieceGrid> WorkpieceGrid::create(WorkpieceGridShape shape) {
    if (!is_valid_shape(shape)) {
        return core::Result<WorkpieceGrid>::failure(
            "workpiece.invalid_shape",
            "workpiece grid dimensions must be between 1 and 64 cells per axis");
    }

    return core::Result<WorkpieceGrid>::success(WorkpieceGrid(shape));
}

WorkpieceGrid::WorkpieceGrid(WorkpieceGridShape shape)
    : shape_(shape), cells_(cell_count(shape), WorkpieceCell::empty()) {}

WorkpieceGridShape WorkpieceGrid::shape() const noexcept {
    return shape_;
}

core::Result<WorkpieceCell> WorkpieceGrid::get(WorkpieceCellCoord coord) const {
    if (!contains(coord)) {
        return core::Result<WorkpieceCell>::failure(
            "workpiece.coord_out_of_bounds", "cell coordinate is outside the workpiece grid");
    }

    return core::Result<WorkpieceCell>::success(cells_[index_of(coord)]);
}

std::size_t WorkpieceGrid::occupied_count() const noexcept {
    return occupied_count_;
}

const std::vector<WorkpieceOperation>& WorkpieceGrid::history() const noexcept {
    return history_;
}

std::uint64_t WorkpieceGrid::mesh_revision() const noexcept {
    return mesh_revision_;
}

core::Status WorkpieceGrid::apply(const WorkpieceOperation& operation) {
    if (!contains(operation.coord)) {
        return core::Status::failure("workpiece.coord_out_of_bounds",
                                     "cell coordinate is outside the workpiece grid");
    }
    if (mesh_revision_ == std::numeric_limits<std::uint64_t>::max() ||
        history_.size() >= 1'000'000U) {
        return core::Status::failure("workpiece.history_exhausted",
                                     "workpiece edit history or revision is exhausted");
    }

    auto& current = cells_[index_of(operation.coord)];
    const auto was_occupied = current.is_occupied();

    switch (operation.kind) {
    case WorkpieceOperationKind::add_cell:
        if (was_occupied) {
            return core::Status::failure("workpiece.cell_occupied",
                                         "cannot add a workpiece cell where one already exists");
        }
        if (!operation.cell.is_occupied()) {
            return core::Status::failure("workpiece.empty_add",
                                         "add_cell requires an occupied replacement cell");
        }
        current = operation.cell;
        ++occupied_count_;
        break;
    case WorkpieceOperationKind::remove_cell:
        if (!was_occupied) {
            return core::Status::failure("workpiece.cell_empty",
                                         "cannot remove an empty workpiece cell");
        }
        current = WorkpieceCell::empty();
        --occupied_count_;
        break;
    case WorkpieceOperationKind::set_cell: {
        const auto will_be_occupied = operation.cell.is_occupied();
        current = operation.cell;
        if (was_occupied && !will_be_occupied) {
            --occupied_count_;
        } else if (!was_occupied && will_be_occupied) {
            ++occupied_count_;
        }
        break;
    }
    }

    history_.push_back(operation);
    ++mesh_revision_;
    return core::Status::ok();
}

bool WorkpieceGrid::contains(WorkpieceCellCoord coord) const noexcept {
    return coord.x < shape_.width && coord.y < shape_.height && coord.z < shape_.depth;
}

std::size_t WorkpieceGrid::index_of(WorkpieceCellCoord coord) const noexcept {
    return static_cast<std::size_t>(coord.z) * static_cast<std::size_t>(shape_.width) *
               static_cast<std::size_t>(shape_.height) +
           static_cast<std::size_t>(coord.y) * static_cast<std::size_t>(shape_.width) +
           static_cast<std::size_t>(coord.x);
}

} // namespace heartstead::workpieces
