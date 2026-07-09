#include "engine/rooms/room_extraction.hpp"

#include <algorithm>
#include <initializer_list>
#include <queue>
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace heartstead::rooms {

namespace {

constexpr std::uint16_t max_axis = 128;

[[nodiscard]] bool valid_bounds(RoomExtractionBounds bounds) noexcept {
    return bounds.width > 0 && bounds.height > 0 && bounds.depth > 0 && bounds.width <= max_axis &&
           bounds.height <= max_axis && bounds.depth <= max_axis;
}

[[nodiscard]] std::size_t cell_count(RoomExtractionBounds bounds) noexcept {
    return static_cast<std::size_t>(bounds.width) * static_cast<std::size_t>(bounds.height) *
           static_cast<std::size_t>(bounds.depth);
}

[[nodiscard]] std::uint32_t per_mille(std::uint64_t numerator, std::uint64_t denominator) noexcept {
    if (denominator == 0) {
        return 0;
    }
    return static_cast<std::uint32_t>(
        std::min<std::uint64_t>(1000, (numerator * 1000u) / denominator));
}

[[nodiscard]] std::uint32_t average_per_mille(std::uint64_t sum, std::uint32_t count) noexcept {
    if (count == 0) {
        return 0;
    }
    return static_cast<std::uint32_t>(std::min<std::uint64_t>(1000, sum / count));
}

[[nodiscard]] std::int32_t average_signed(std::int64_t sum, std::uint32_t count) noexcept {
    if (count == 0) {
        return 0;
    }
    return static_cast<std::int32_t>(sum / static_cast<std::int64_t>(count));
}

[[nodiscard]] std::uint32_t voxel_light_per_mille(world::VoxelCell voxel) noexcept {
    return static_cast<std::uint32_t>(
        std::min<std::uint32_t>(1000u, (static_cast<std::uint32_t>(voxel.light) * 1000u) / 255u));
}

[[nodiscard]] bool has_tag(const std::vector<std::string>& tags, std::string_view tag) noexcept {
    return std::ranges::any_of(
        tags, [tag](const std::string& candidate) { return std::string_view(candidate) == tag; });
}

[[nodiscard]] bool has_any_tag(const std::vector<std::string>& tags,
                               std::initializer_list<std::string_view> candidates) noexcept {
    return std::ranges::any_of(candidates,
                               [&tags](std::string_view tag) { return has_tag(tags, tag); });
}

[[nodiscard]] bool build_piece_included(build::ConstructionState state,
                                        RoomExtractionSourceConfig config) noexcept {
    switch (state) {
    case build::ConstructionState::planned:
        return config.include_planned_build_pieces;
    case build::ConstructionState::under_construction:
        return config.include_under_construction_build_pieces;
    case build::ConstructionState::complete:
        return true;
    case build::ConstructionState::damaged:
        return config.include_damaged_build_pieces;
    }
    return false;
}

[[nodiscard]] bool solid_room_contribution(const build::BuildPieceRecord& build_piece) noexcept {
    return has_any_tag(build_piece.room_contribution_tags,
                       {"solid", "wall", "enclosure", "floor", "foundation", "roof"});
}

void append_source_id(RoomExtractionCell& cell, core::SaveId id) {
    if (!id.is_valid()) {
        return;
    }
    const auto already_present = std::ranges::any_of(
        cell.source_build_piece_ids, [id](core::SaveId existing) { return existing == id; });
    if (!already_present) {
        cell.source_build_piece_ids.push_back(id);
    }
}

void apply_network_port(RoomExtractionCell& cell, const build::BuildNetworkPort& port) noexcept {
    switch (port.kind) {
    case networks::NetworkKind::storage_access:
        cell.storage_access = true;
        break;
    case networks::NetworkKind::cart_access:
    case networks::NetworkKind::road:
    case networks::NetworkKind::logistics:
        cell.cart_access = true;
        break;
    case networks::NetworkKind::power:
        cell.power_access = true;
        break;
    case networks::NetworkKind::ward:
        cell.ward_coverage = true;
        break;
    case networks::NetworkKind::smoke_ventilation:
        cell.ventilation_per_mille = std::max<std::uint32_t>(cell.ventilation_per_mille, 650);
        break;
    case networks::NetworkKind::water:
        break;
    }
}

void apply_room_contribution_tags(RoomExtractionCell& cell,
                                  const build::BuildPieceRecord& build_piece) {
    const auto& tags = build_piece.room_contribution_tags;
    if (solid_room_contribution(build_piece)) {
        cell.solid = true;
    }
    if (has_tag(tags, "roof")) {
        cell.roofed = true;
    }
    if (has_any_tag(tags, {"storage_access", "storage"})) {
        cell.storage_access = true;
    }
    if (has_any_tag(tags, {"cart_access", "road"})) {
        cell.cart_access = true;
    }
    if (has_any_tag(tags, {"power_access", "power"})) {
        cell.power_access = true;
    }
    if (has_any_tag(tags, {"ward_coverage", "ward"})) {
        cell.ward_coverage = true;
    }
    if (has_any_tag(tags, {"ventilation", "vent"})) {
        cell.ventilation_per_mille = std::max<std::uint32_t>(cell.ventilation_per_mille, 650);
    }
    if (has_any_tag(tags, {"warm", "heat"})) {
        cell.warmth = std::max<std::int32_t>(cell.warmth, 250);
    }
    if (has_tag(tags, "dry")) {
        cell.dryness = std::max<std::int32_t>(cell.dryness, 250);
    }
}

void append_unique_source_ids(std::set<std::uint64_t>& seen, std::vector<core::SaveId>& output,
                              const std::vector<core::SaveId>& ids) {
    for (const auto id : ids) {
        if (id.is_valid() && seen.insert(id.value()).second) {
            output.push_back(id);
        }
    }
}

struct Neighbor {
    RoomCellCoord coord;
    bool inside = false;
};

[[nodiscard]] std::vector<Neighbor> neighbors_of(const RoomExtractionGrid& grid,
                                                 RoomCellCoord coord) {
    std::vector<Neighbor> result;
    result.reserve(6);

    const auto bounds = grid.bounds();
    const auto inside = [bounds](RoomCellCoord candidate) {
        return candidate.x < bounds.width && candidate.y < bounds.height &&
               candidate.z < bounds.depth;
    };
    const auto push = [&result, &inside](RoomCellCoord candidate) {
        result.push_back(Neighbor{candidate, inside(candidate)});
    };

    push({static_cast<std::uint16_t>(coord.x - (coord.x > 0 ? 1 : 0)), coord.y, coord.z});
    if (coord.x == 0) {
        result.back().inside = false;
    }
    push({static_cast<std::uint16_t>(coord.x + 1), coord.y, coord.z});
    if (coord.x + 1 >= bounds.width) {
        result.back().inside = false;
    }

    push({coord.x, static_cast<std::uint16_t>(coord.y - (coord.y > 0 ? 1 : 0)), coord.z});
    if (coord.y == 0) {
        result.back().inside = false;
    }
    push({coord.x, static_cast<std::uint16_t>(coord.y + 1), coord.z});
    if (coord.y + 1 >= bounds.height) {
        result.back().inside = false;
    }

    push({coord.x, coord.y, static_cast<std::uint16_t>(coord.z - (coord.z > 0 ? 1 : 0))});
    if (coord.z == 0) {
        result.back().inside = false;
    }
    push({coord.x, coord.y, static_cast<std::uint16_t>(coord.z + 1)});
    if (coord.z + 1 >= bounds.depth) {
        result.back().inside = false;
    }

    return result;
}

struct Accumulator {
    std::uint32_t volume = 0;
    std::uint64_t roofed = 0;
    std::uint64_t wall_faces = 0;
    std::uint64_t open_faces = 0;
    std::int64_t warmth_sum = 0;
    std::int64_t dryness_sum = 0;
    std::uint64_t light_sum = 0;
    std::uint64_t smoke_sum = 0;
    std::uint64_t ventilation_sum = 0;
    std::uint64_t safety_sum = 0;
    bool storage_access = false;
    bool cart_access = false;
    bool power_access = false;
    bool ward_coverage = false;
    std::set<std::uint64_t> source_seen;
    std::vector<core::SaveId> source_ids;
};

} // namespace

core::Result<RoomExtractionGrid> RoomExtractionGrid::create(RoomExtractionBounds bounds) {
    if (!valid_bounds(bounds)) {
        return core::Result<RoomExtractionGrid>::failure(
            "room_extraction.invalid_bounds",
            "room extraction bounds must be between 1 and 128 cells per axis");
    }
    return core::Result<RoomExtractionGrid>::success(RoomExtractionGrid(bounds));
}

RoomExtractionGrid::RoomExtractionGrid(RoomExtractionBounds bounds)
    : bounds_(bounds), cells_(cell_count(bounds)) {}

RoomExtractionBounds RoomExtractionGrid::bounds() const noexcept {
    return bounds_;
}

core::Result<RoomExtractionCell> RoomExtractionGrid::cell(RoomCellCoord coord) const {
    if (!contains(coord)) {
        return core::Result<RoomExtractionCell>::failure(
            "room_extraction.coord_out_of_bounds",
            "room extraction coordinate is outside the grid");
    }
    return core::Result<RoomExtractionCell>::success(cells_[index_of(coord)]);
}

core::Status RoomExtractionGrid::set_cell(RoomCellCoord coord, RoomExtractionCell cell) {
    if (!contains(coord)) {
        return core::Status::failure("room_extraction.coord_out_of_bounds",
                                     "room extraction coordinate is outside the grid");
    }
    cells_[index_of(coord)] = std::move(cell);
    return core::Status::ok();
}

bool RoomExtractionGrid::contains(RoomCellCoord coord) const noexcept {
    return coord.x < bounds_.width && coord.y < bounds_.height && coord.z < bounds_.depth;
}

std::size_t RoomExtractionGrid::index_of(RoomCellCoord coord) const noexcept {
    return static_cast<std::size_t>(coord.z) * static_cast<std::size_t>(bounds_.width) *
               static_cast<std::size_t>(bounds_.height) +
           static_cast<std::size_t>(coord.y) * static_cast<std::size_t>(bounds_.width) +
           static_cast<std::size_t>(coord.x);
}

core::Result<RoomExtractionGridBuilder>
RoomExtractionGridBuilder::create(RoomExtractionBounds bounds, RoomExtractionSourceConfig config) {
    auto grid = RoomExtractionGrid::create(bounds);
    if (!grid) {
        return core::Result<RoomExtractionGridBuilder>::failure(grid.error().code,
                                                                grid.error().message);
    }
    return core::Result<RoomExtractionGridBuilder>::success(
        RoomExtractionGridBuilder(std::move(grid).value(), config));
}

RoomExtractionGridBuilder::RoomExtractionGridBuilder(RoomExtractionGrid grid,
                                                     RoomExtractionSourceConfig config)
    : grid_(std::move(grid)), config_(config) {}

core::Status RoomExtractionGridBuilder::apply_terrain_voxel(RoomCellCoord coord,
                                                            world::VoxelCell voxel) {
    auto cell = grid_.cell(coord);
    if (!cell) {
        return core::Status::failure(cell.error().code, cell.error().message);
    }

    auto updated = std::move(cell).value();
    updated.solid = updated.solid || !voxel.is_air();
    updated.light_per_mille =
        std::max<std::uint32_t>(updated.light_per_mille, voxel_light_per_mille(voxel));
    return grid_.set_cell(coord, std::move(updated));
}

core::Status
RoomExtractionGridBuilder::apply_build_piece(const build::BuildPieceRecord& build_piece,
                                             RoomCellCoord coord) {
    auto status = build_piece.validate();
    if (!status) {
        return status;
    }
    if (!build_piece_included(build_piece.construction_state, config_)) {
        return core::Status::ok();
    }

    auto cell = grid_.cell(coord);
    if (!cell) {
        return core::Status::failure(cell.error().code, cell.error().message);
    }

    auto updated = std::move(cell).value();
    append_source_id(updated, build_piece.object_id);
    apply_room_contribution_tags(updated, build_piece);
    for (const auto& port : build_piece.network_ports) {
        apply_network_port(updated, port);
    }

    status = grid_.set_cell(coord, std::move(updated));
    if (!status) {
        return status;
    }

    if (has_tag(build_piece.room_contribution_tags, "roof") && coord.y > 0) {
        const RoomCellCoord covered{coord.x, static_cast<std::uint16_t>(coord.y - 1), coord.z};
        auto covered_cell = grid_.cell(covered);
        if (!covered_cell) {
            return core::Status::failure(covered_cell.error().code, covered_cell.error().message);
        }
        auto covered_update = std::move(covered_cell).value();
        covered_update.roofed = true;
        append_source_id(covered_update, build_piece.object_id);
        return grid_.set_cell(covered, std::move(covered_update));
    }

    return core::Status::ok();
}

core::Status RoomExtractionGridBuilder::apply_build_piece_footprint(
    const build::BuildPieceRecord& build_piece, const std::vector<RoomCellCoord>& footprint) {
    if (footprint.empty()) {
        return core::Status::failure("room_extraction.empty_build_piece_footprint",
                                     "build piece room extraction footprint must not be empty");
    }
    for (const auto coord : footprint) {
        auto status = apply_build_piece(build_piece, coord);
        if (!status) {
            return status;
        }
    }
    return core::Status::ok();
}

const RoomExtractionGrid& RoomExtractionGridBuilder::grid() const noexcept {
    return grid_;
}

RoomExtractionGrid&& RoomExtractionGridBuilder::take_grid() && noexcept {
    return std::move(grid_);
}

core::Result<RoomGraph> RoomExtractor::extract(const RoomExtractionGrid& grid,
                                               RoomExtractionConfig config) {
    if (!config.first_room_id.is_valid()) {
        return core::Result<RoomGraph>::failure("room_extraction.invalid_first_room_id",
                                                "first room id must be valid");
    }
    if (config.minimum_volume_cells == 0) {
        return core::Result<RoomGraph>::failure("room_extraction.invalid_minimum_volume",
                                                "minimum room volume must be non-zero");
    }

    RoomGraph graph;
    std::vector<bool> visited(grid.cells_.size(), false);
    std::uint64_t next_room_id = config.first_room_id.value();

    for (std::size_t start_index = 0; start_index < grid.cells_.size(); ++start_index) {
        if (visited[start_index] || grid.cells_[start_index].solid) {
            continue;
        }

        Accumulator accumulator;
        std::queue<std::size_t> queue;
        queue.push(start_index);
        visited[start_index] = true;

        while (!queue.empty()) {
            const auto index = queue.front();
            queue.pop();

            const auto bounds = grid.bounds();
            const auto xy = static_cast<std::size_t>(bounds.width) * bounds.height;
            const auto z = static_cast<std::uint16_t>(index / xy);
            const auto remainder = index % xy;
            const auto y = static_cast<std::uint16_t>(remainder / bounds.width);
            const auto x = static_cast<std::uint16_t>(remainder % bounds.width);
            const RoomCellCoord coord{x, y, z};
            const auto& cell = grid.cells_[index];

            ++accumulator.volume;
            accumulator.roofed += cell.roofed ? 1u : 0u;
            accumulator.warmth_sum += cell.warmth;
            accumulator.dryness_sum += cell.dryness;
            accumulator.light_sum += cell.light_per_mille;
            accumulator.smoke_sum += cell.smoke_per_mille;
            accumulator.ventilation_sum += cell.ventilation_per_mille;
            accumulator.safety_sum += cell.safety_per_mille;
            accumulator.storage_access = accumulator.storage_access || cell.storage_access;
            accumulator.cart_access = accumulator.cart_access || cell.cart_access;
            accumulator.power_access = accumulator.power_access || cell.power_access;
            accumulator.ward_coverage = accumulator.ward_coverage || cell.ward_coverage;
            append_unique_source_ids(accumulator.source_seen, accumulator.source_ids,
                                     cell.source_build_piece_ids);

            for (const auto neighbor : neighbors_of(grid, coord)) {
                if (!neighbor.inside) {
                    ++accumulator.open_faces;
                    continue;
                }

                const auto neighbor_index = grid.index_of(neighbor.coord);
                const auto& neighbor_cell = grid.cells_[neighbor_index];
                if (neighbor_cell.solid) {
                    ++accumulator.wall_faces;
                    append_unique_source_ids(accumulator.source_seen, accumulator.source_ids,
                                             neighbor_cell.source_build_piece_ids);
                    continue;
                }
                if (!visited[neighbor_index]) {
                    visited[neighbor_index] = true;
                    queue.push(neighbor_index);
                }
            }
        }

        if (accumulator.volume < config.minimum_volume_cells) {
            continue;
        }

        RoomRecord room;
        room.id = RoomId::from_value(next_room_id++);
        room.label = "Room " + room.id.to_string();
        room.volume_cells = accumulator.volume;
        room.source_build_piece_ids = std::move(accumulator.source_ids);
        const auto boundary_faces = accumulator.wall_faces + accumulator.open_faces;
        room.metrics.enclosure_per_mille = per_mille(accumulator.wall_faces, boundary_faces);
        room.metrics.wall_coverage_per_mille = room.metrics.enclosure_per_mille;
        room.metrics.roof_coverage_per_mille = per_mille(accumulator.roofed, accumulator.volume);
        room.metrics.warmth = average_signed(accumulator.warmth_sum, accumulator.volume);
        room.metrics.dryness = average_signed(accumulator.dryness_sum, accumulator.volume);
        room.metrics.light_per_mille = average_per_mille(accumulator.light_sum, accumulator.volume);
        room.metrics.smoke_per_mille = average_per_mille(accumulator.smoke_sum, accumulator.volume);
        room.metrics.ventilation_per_mille =
            average_per_mille(accumulator.ventilation_sum, accumulator.volume);
        room.metrics.safety_per_mille =
            average_per_mille(accumulator.safety_sum, accumulator.volume);
        room.metrics.spaciousness_per_mille =
            std::min<std::uint32_t>(1000, accumulator.volume * 100u);
        room.metrics.storage_access = accumulator.storage_access;
        room.metrics.cart_access = accumulator.cart_access;
        room.metrics.power_access = accumulator.power_access;
        room.metrics.ward_coverage = accumulator.ward_coverage;

        auto status = graph.add_or_replace(std::move(room));
        if (!status) {
            return core::Result<RoomGraph>::failure(status.error().code, status.error().message);
        }
    }

    graph.evaluate_all();
    return core::Result<RoomGraph>::success(std::move(graph));
}

} // namespace heartstead::rooms
