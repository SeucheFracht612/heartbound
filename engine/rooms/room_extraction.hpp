#pragma once

#include "engine/build/build_piece.hpp"
#include "engine/core/ids.hpp"
#include "engine/core/result.hpp"
#include "engine/rooms/room_graph.hpp"
#include "engine/world/voxels/voxel_chunk.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace heartstead::rooms {

struct RoomCellCoord {
    std::uint16_t x = 0;
    std::uint16_t y = 0;
    std::uint16_t z = 0;
};

struct RoomExtractionBounds {
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::uint16_t depth = 0;
};

struct RoomExtractionCell {
    bool solid = false;
    bool roofed = false;
    std::int32_t warmth = 0;
    std::int32_t dryness = 0;
    std::uint32_t light_per_mille = 0;
    std::uint32_t smoke_per_mille = 0;
    std::uint32_t ventilation_per_mille = 0;
    std::uint32_t safety_per_mille = 1000;
    bool storage_access = false;
    bool cart_access = false;
    bool power_access = false;
    bool ward_coverage = false;
    std::vector<core::SaveId> source_build_piece_ids;
};

class RoomExtractionGrid {
  public:
    [[nodiscard]] static core::Result<RoomExtractionGrid> create(RoomExtractionBounds bounds);

    [[nodiscard]] RoomExtractionBounds bounds() const noexcept;
    [[nodiscard]] core::Result<RoomExtractionCell> cell(RoomCellCoord coord) const;
    [[nodiscard]] core::Status set_cell(RoomCellCoord coord, RoomExtractionCell cell);

  private:
    explicit RoomExtractionGrid(RoomExtractionBounds bounds);

    [[nodiscard]] bool contains(RoomCellCoord coord) const noexcept;
    [[nodiscard]] std::size_t index_of(RoomCellCoord coord) const noexcept;

    RoomExtractionBounds bounds_;
    std::vector<RoomExtractionCell> cells_;

    friend class RoomExtractor;
};

struct RoomExtractionSourceConfig {
    bool include_planned_build_pieces = false;
    bool include_under_construction_build_pieces = false;
    bool include_damaged_build_pieces = true;
};

class RoomExtractionGridBuilder {
  public:
    [[nodiscard]] static core::Result<RoomExtractionGridBuilder>
    create(RoomExtractionBounds bounds, RoomExtractionSourceConfig config = {});

    [[nodiscard]] core::Status apply_terrain_voxel(RoomCellCoord coord, world::VoxelCell voxel);
    [[nodiscard]] core::Status apply_build_piece(const build::BuildPieceRecord& build_piece,
                                                 RoomCellCoord coord);
    [[nodiscard]] core::Status
    apply_build_piece_footprint(const build::BuildPieceRecord& build_piece,
                                const std::vector<RoomCellCoord>& footprint);

    [[nodiscard]] const RoomExtractionGrid& grid() const noexcept;
    [[nodiscard]] RoomExtractionGrid&& take_grid() && noexcept;

  private:
    RoomExtractionGridBuilder(RoomExtractionGrid grid, RoomExtractionSourceConfig config);

    RoomExtractionGrid grid_;
    RoomExtractionSourceConfig config_;
};

struct RoomExtractionConfig {
    RoomId first_room_id = RoomId::from_value(1);
    std::uint32_t minimum_volume_cells = 1;
};

class RoomExtractor {
  public:
    [[nodiscard]] static core::Result<RoomGraph> extract(const RoomExtractionGrid& grid,
                                                         RoomExtractionConfig config = {});
};

} // namespace heartstead::rooms
