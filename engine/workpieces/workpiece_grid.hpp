#pragma once

#include "engine/core/result.hpp"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace heartstead::workpieces {

struct WorkpieceGridShape {
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::uint16_t depth = 0;

    friend auto operator<=>(const WorkpieceGridShape&, const WorkpieceGridShape&) = default;
};

struct WorkpieceCellCoord {
    std::uint16_t x = 0;
    std::uint16_t y = 0;
    std::uint16_t z = 0;

    friend auto operator<=>(const WorkpieceCellCoord&, const WorkpieceCellCoord&) = default;
};

struct WorkpieceCell {
    std::uint16_t material = 0;
    std::uint8_t occupancy = 0;
    std::uint16_t pattern_id = 0;
    std::uint8_t flags = 0;

    [[nodiscard]] static constexpr WorkpieceCell empty() noexcept {
        return WorkpieceCell{};
    }

    [[nodiscard]] static constexpr WorkpieceCell solid(std::uint16_t material_id) noexcept {
        return WorkpieceCell{material_id, 255, 0, 0};
    }

    [[nodiscard]] constexpr bool is_occupied() const noexcept {
        return occupancy > 0;
    }

    friend auto operator<=>(const WorkpieceCell&, const WorkpieceCell&) = default;
};

enum class WorkpieceOperationKind {
    add_cell,
    remove_cell,
    set_cell,
};

struct WorkpieceOperation {
    WorkpieceOperationKind kind = WorkpieceOperationKind::set_cell;
    WorkpieceCellCoord coord;
    WorkpieceCell cell;
};

class WorkpieceGrid {
  public:
    [[nodiscard]] static core::Result<WorkpieceGrid> create(WorkpieceGridShape shape);

    [[nodiscard]] WorkpieceGridShape shape() const noexcept;
    [[nodiscard]] core::Result<WorkpieceCell> get(WorkpieceCellCoord coord) const;
    [[nodiscard]] std::size_t occupied_count() const noexcept;
    [[nodiscard]] const std::vector<WorkpieceOperation>& history() const noexcept;
    [[nodiscard]] std::uint64_t mesh_revision() const noexcept;

    [[nodiscard]] core::Status apply(const WorkpieceOperation& operation);

  private:
    explicit WorkpieceGrid(WorkpieceGridShape shape);

    [[nodiscard]] bool contains(WorkpieceCellCoord coord) const noexcept;
    [[nodiscard]] std::size_t index_of(WorkpieceCellCoord coord) const noexcept;

    WorkpieceGridShape shape_;
    std::vector<WorkpieceCell> cells_;
    std::vector<WorkpieceOperation> history_;
    std::size_t occupied_count_ = 0;
    std::uint64_t mesh_revision_ = 0;
};

enum class WorkpieceCellFlag : std::uint8_t {
    smoothed = 1U << 0U,
    decorated = 1U << 1U,
    revealed = 1U << 2U,
};

[[nodiscard]] constexpr bool has_workpiece_cell_flag(WorkpieceCell cell,
                                                     WorkpieceCellFlag flag) noexcept {
    return (cell.flags & static_cast<std::uint8_t>(flag)) != 0;
}

} // namespace heartstead::workpieces
