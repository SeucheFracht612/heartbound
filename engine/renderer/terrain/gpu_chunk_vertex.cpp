#include "engine/renderer/terrain/gpu_chunk_vertex.hpp"

namespace heartstead::renderer::terrain {

GpuChunkVertex to_gpu_chunk_vertex(const world::ChunkMeshVertex& vertex) noexcept {
    return GpuChunkVertex{
        {vertex.position.x, vertex.position.y, vertex.position.z},
        {vertex.normal.x, vertex.normal.y, vertex.normal.z},
        {vertex.u, vertex.v},
        vertex.voxel_type,
        vertex.light,
        0,
        vertex.state_bits,
        0,
    };
}

std::vector<GpuChunkVertex>
make_gpu_chunk_vertices(const std::vector<world::ChunkMeshVertex>& vertices) {
    std::vector<GpuChunkVertex> result;
    result.reserve(vertices.size());
    for (const auto& vertex : vertices) {
        result.push_back(to_gpu_chunk_vertex(vertex));
    }
    return result;
}

} // namespace heartstead::renderer::terrain
