#pragma once

#include "engine/core/result.hpp"
#include "engine/world/regions/region_graph.hpp"
#include "engine/world/voxels/voxel_chunk.hpp"
#include "engine/world/voxels/voxel_palette.hpp"

#include <cstdint>
#include <string>

namespace heartstead::world {

struct TerrainGenerationConfig {
    std::uint64_t world_seed = 0;
    std::string region_id;
    // Authoritative vertical identity remains integral at far-world coordinates.
    std::int64_t base_surface_y = 0;
    std::uint16_t surface_variation = 0;
};

class DeterministicTerrainGenerator {
  public:
    [[nodiscard]] static core::Result<VoxelChunk>
    generate_chunk(ChunkCoord coord, const TerrainGenerationConfig& config,
                   const RegionGraph& regions, const VoxelPalette& palette);

    [[nodiscard]] static std::int64_t surface_height_at(const TerrainGenerationConfig& config,
                                                        std::int64_t global_x,
                                                        std::int64_t global_z) noexcept;
};

} // namespace heartstead::world
