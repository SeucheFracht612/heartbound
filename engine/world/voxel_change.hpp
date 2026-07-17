#pragma once

#include "engine/core/result.hpp"
#include "engine/world/chunks/chunk_identity.hpp"
#include "engine/world/coords/world_coords.hpp"
#include "engine/world/voxels/voxel_chunk.hpp"

#include <string>
#include <string_view>

namespace heartstead::world {

inline constexpr std::string_view voxel_changed_event_type = "world.voxel_changed.v1";

struct VoxelChangeRecord {
    BlockCoord position;
    VoxelCell previous;
    VoxelCell current;
    ChunkIdentity chunk_identity;
    std::uint64_t content_revision = 0;

    [[nodiscard]] core::Status validate() const;
};

class VoxelChangeTextCodec {
  public:
    [[nodiscard]] static std::string encode(const VoxelChangeRecord& change);
    [[nodiscard]] static core::Result<VoxelChangeRecord> decode(std::string_view payload);
};

} // namespace heartstead::world
