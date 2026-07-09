#include "engine/world/worldgen/terrain_generator.hpp"

#include "engine/core/hash.hpp"

#include <limits>
#include <string_view>
#include <utility>
#include <vector>

namespace heartstead::world {

namespace {

[[nodiscard]] std::uint64_t mix(std::uint64_t value) noexcept {
    value ^= value >> 30U;
    value *= 0xbf58476d1ce4e5b9ULL;
    value ^= value >> 27U;
    value *= 0x94d049bb133111ebULL;
    value ^= value >> 31U;
    return value;
}

[[nodiscard]] std::uint64_t stable_string_hash(std::string_view value) noexcept {
    return core::stable_hash64(value);
}

[[nodiscard]] std::uint64_t coordinate_bits(std::int64_t value) noexcept {
    return static_cast<std::uint64_t>(value) ^ 0x8000000000000000ULL;
}

[[nodiscard]] std::int64_t saturated_add(std::int64_t value, std::int64_t offset) noexcept {
    constexpr auto min = std::numeric_limits<std::int64_t>::min();
    constexpr auto max = std::numeric_limits<std::int64_t>::max();
    if (offset > 0 && value > max - offset) {
        return max;
    }
    if (offset < 0 && value < min - offset) {
        return min;
    }
    return value + offset;
}

[[nodiscard]] core::Result<VoxelCell> select_region_terrain_cell(const RegionDescriptor& region,
                                                                 const VoxelPalette& palette) {
    for (const auto& rule : region.resource_rules) {
        auto cell = palette.cell_for(rule.prototype_id);
        if (cell) {
            return cell;
        }
    }

    return core::Result<VoxelCell>::failure(
        "terrain_generator.missing_region_voxel",
        "region has no resource rule that resolves to a voxel palette entry: " + region.id);
}

} // namespace

core::Result<VoxelChunk> DeterministicTerrainGenerator::generate_chunk(
    ChunkCoord coord, const TerrainGenerationConfig& config, const RegionGraph& regions,
    const VoxelPalette& palette) {
    if (config.region_id.empty()) {
        return core::Result<VoxelChunk>::failure("terrain_generator.missing_region_id",
                                                 "terrain generation requires a region id");
    }
    if (palette.empty()) {
        return core::Result<VoxelChunk>::failure("terrain_generator.empty_palette",
                                                 "terrain generation requires a voxel palette");
    }

    const auto* region = regions.find(config.region_id);
    if (region == nullptr) {
        return core::Result<VoxelChunk>::failure("terrain_generator.missing_region",
                                                 "terrain generation region does not exist: " +
                                                     config.region_id);
    }

    auto terrain_cell = select_region_terrain_cell(*region, palette);
    if (!terrain_cell) {
        return core::Result<VoxelChunk>::failure(terrain_cell.error().code,
                                                 terrain_cell.error().message);
    }

    auto chunk_origin = chunk_local_to_block(coord, {0, 0, 0});
    auto chunk_max =
        chunk_local_to_block(coord, {VoxelChunk::edge_length - 1, VoxelChunk::edge_length - 1,
                                     VoxelChunk::edge_length - 1});
    if (!chunk_origin || !chunk_max) {
        return core::Result<VoxelChunk>::failure(
            "terrain_generator.chunk_coord_overflow",
            "chunk coordinate cannot be represented by signed 64-bit block coordinates");
    }

    std::vector<VoxelCell> cells;
    cells.reserve(VoxelChunk::total_cells);
    for (std::uint16_t z = 0; z < VoxelChunk::edge_length; ++z) {
        const auto global_z = chunk_origin.value().z + static_cast<std::int64_t>(z);
        for (std::uint16_t y = 0; y < VoxelChunk::edge_length; ++y) {
            const auto global_y = chunk_origin.value().y + static_cast<std::int64_t>(y);
            for (std::uint16_t x = 0; x < VoxelChunk::edge_length; ++x) {
                const auto global_x = chunk_origin.value().x + static_cast<std::int64_t>(x);
                const auto surface_y = surface_height_at(config, global_x, global_z);
                cells.push_back(global_y <= surface_y ? terrain_cell.value()
                                                      : VoxelCell{VoxelPalette::air_type, 255});
            }
        }
    }

    VoxelChunk chunk(coord);
    auto status = chunk.load_generated_cells(std::move(cells));
    if (!status) {
        return core::Result<VoxelChunk>::failure(status.error().code, status.error().message);
    }
    return core::Result<VoxelChunk>::success(std::move(chunk));
}

std::int64_t DeterministicTerrainGenerator::surface_height_at(const TerrainGenerationConfig& config,
                                                              std::int64_t global_x,
                                                              std::int64_t global_z) noexcept {
    if (config.surface_variation == 0) {
        return config.base_surface_y;
    }

    const auto hash = mix(config.world_seed) ^ mix(coordinate_bits(global_x)) ^
                      mix(coordinate_bits(global_z) << 1U) ^
                      mix(stable_string_hash(config.region_id));
    const auto range = static_cast<std::uint64_t>(config.surface_variation) * 2ULL + 1ULL;
    const auto offset = static_cast<std::int64_t>(mix(hash) % range) -
                        static_cast<std::int64_t>(config.surface_variation);
    return saturated_add(config.base_surface_y, offset);
}

} // namespace heartstead::world
