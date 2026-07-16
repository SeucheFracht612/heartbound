#pragma once

#include "engine/core/result.hpp"
#include "engine/world/meshing/chunk_mesh_snapshot.hpp"
#include "engine/world/meshing/chunk_mesher.hpp"

namespace heartstead::world {

// Optimized immutable-snapshot mesher. The reference ChunkMesher remains available for
// correctness comparisons and for geometry families that do not yet have an optimized emitter.
class GreedyChunkMesher {
  public:
    [[nodiscard]] static core::Result<ChunkMesh>
    build_surface_mesh(const ChunkNeighborhoodSnapshot& neighborhood,
                       const BlockRenderTableSnapshot& render_table);
    [[nodiscard]] static core::Result<ChunkMesh>
    build_surface_mesh(const ChunkNeighborhoodSnapshot& neighborhood,
                       const BlockRenderTableSnapshot& render_table, ChunkMesh reusable_mesh);
};

} // namespace heartstead::world
