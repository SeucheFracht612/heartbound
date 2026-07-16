#pragma once

#include "engine/math/vector.hpp"
#include "engine/renderer/rhi/render_device.hpp"
#include "engine/world/meshing/chunk_mesher.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

namespace heartstead::renderer::terrain {

inline constexpr float terrain_vertex_fixed_scale = 256.0F;

// Compact production terrain ABI (24 bytes). Shader locations are part of the contract:
//   0: ivec4 fixed-point local position (xyz / 256)
//   1: uvec2 fixed-point repeating UV (uv / 256)
//   2: uint material/voxel table index
//   3: uint state bits
//   4: vec4 signed-normalized normal
//   5: uvec4 light, ambient occlusion, quad corner, flags
struct GpuTerrainVertex {
    std::int16_t position[4]{};
    std::uint16_t uv[2]{};
    std::uint16_t material = 0;
    std::uint16_t state_bits = 0;
    std::int8_t normal[4]{};
    std::uint8_t lighting[4]{};
};

using GpuChunkVertex = GpuTerrainVertex;

static_assert(std::is_standard_layout_v<GpuTerrainVertex>);
static_assert(sizeof(GpuTerrainVertex) == 24);
static_assert(alignof(GpuTerrainVertex) == alignof(std::uint16_t));
static_assert(offsetof(GpuTerrainVertex, position) == 0);
static_assert(offsetof(GpuTerrainVertex, uv) == 8);
static_assert(offsetof(GpuTerrainVertex, material) == 12);
static_assert(offsetof(GpuTerrainVertex, state_bits) == 14);
static_assert(offsetof(GpuTerrainVertex, normal) == 16);
static_assert(offsetof(GpuTerrainVertex, lighting) == 20);

inline constexpr std::array<rhi::RenderVertexAttributeDesc, 6> gpu_chunk_vertex_attributes{
    rhi::RenderVertexAttributeDesc{0, 0, rhi::RenderVertexAttributeFormat::sint16x4},
    rhi::RenderVertexAttributeDesc{1, 8, rhi::RenderVertexAttributeFormat::uint16x2},
    rhi::RenderVertexAttributeDesc{2, 12, rhi::RenderVertexAttributeFormat::uint16},
    rhi::RenderVertexAttributeDesc{3, 14, rhi::RenderVertexAttributeFormat::uint16},
    rhi::RenderVertexAttributeDesc{4, 16, rhi::RenderVertexAttributeFormat::snorm8x4},
    rhi::RenderVertexAttributeDesc{5, 20, rhi::RenderVertexAttributeFormat::uint8x4},
};

[[nodiscard]] GpuTerrainVertex to_gpu_chunk_vertex(const world::ChunkMeshVertex& vertex,
                                                   std::uint8_t corner = 0) noexcept;
[[nodiscard]] std::vector<GpuTerrainVertex>
make_gpu_chunk_vertices(const std::vector<world::ChunkMeshVertex>& vertices);
[[nodiscard]] std::vector<GpuTerrainVertex>
make_gpu_chunk_vertices(const std::vector<world::ChunkMeshVertex>& vertices,
                        std::vector<GpuTerrainVertex> reusable_vertices);
[[nodiscard]] math::Vec3f decode_terrain_vertex_position(const GpuTerrainVertex& vertex) noexcept;
[[nodiscard]] math::Vec3f decode_terrain_vertex_normal(const GpuTerrainVertex& vertex) noexcept;
[[nodiscard]] std::array<float, 2>
decode_terrain_vertex_uv(const GpuTerrainVertex& vertex) noexcept;

} // namespace heartstead::renderer::terrain
