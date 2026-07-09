#pragma once

#include "engine/core/result.hpp"
#include "engine/math/vector.hpp"
#include "engine/world/voxels/voxel_chunk.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace heartstead::world {

enum class ChunkMeshFaceDirection {
    negative_x,
    positive_x,
    negative_y,
    positive_y,
    negative_z,
    positive_z,
};

struct ChunkMeshVertex {
    math::Vec3f position{};
    math::Vec3f normal{};
    float u = 0.0F;
    float v = 0.0F;
    std::uint16_t voxel_type = 0;
    std::uint8_t light = 0;
};

struct ChunkMesh {
    ChunkCoord chunk_coord{};
    math::Bounds3f local_bounds{};
    std::vector<ChunkMeshVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::size_t face_count = 0;

    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] core::Status validate() const;
};

class ChunkMesher {
  public:
    [[nodiscard]] static core::Result<ChunkMesh> build_surface_mesh(const VoxelChunk& chunk);
};

[[nodiscard]] math::Vec3f chunk_mesh_face_normal(ChunkMeshFaceDirection direction) noexcept;

} // namespace heartstead::world
