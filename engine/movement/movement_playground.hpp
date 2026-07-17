#pragma once

#include "engine/core/result.hpp"
#include "engine/world/chunks/chunk_database.hpp"
#include "engine/world/coords/world_position.hpp"
#include "engine/world/voxels/voxel_palette.hpp"

#include <string>
#include <vector>

namespace heartstead::movement {

struct MovementPlaygroundStation {
    std::string name;
    world::WorldPosition position;
    std::string purpose;
};

struct MovementPlaygroundWorld {
    world::VoxelPalette palette;
    world::ChunkDatabase chunks;
    world::WorldPosition spawn;
    std::vector<MovementPlaygroundStation> stations;
};

class MovementPlaygroundBuilder {
  public:
    [[nodiscard]] static core::Result<MovementPlaygroundWorld> build();
};

} // namespace heartstead::movement
