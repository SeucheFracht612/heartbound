#pragma once

#include "engine/renderer/rhi/render_device.hpp"
#include "engine/world/meshing/chunk_mesher.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

namespace heartstead::renderer::terrain {

// Stable GPU ABI for terrain vertices. Shader locations are intentionally part of the contract:
//   0 position (vec3), 1 normal (vec3), 2 uv (vec2), 3 voxel_type (uint),
//   4 light (uint), 5 state_bits (uint).
struct GpuChunkVertex {
    float position[3]{};
    float normal[3]{};
    float uv[2]{};
    std::uint16_t voxel_type = 0;
    std::uint8_t light = 0;
    std::uint8_t padding = 0;
    std::uint16_t state_bits = 0;
    std::uint16_t padding2 = 0;
};

static_assert(std::is_standard_layout_v<GpuChunkVertex>);
static_assert(sizeof(GpuChunkVertex) == 40);
static_assert(alignof(GpuChunkVertex) == alignof(float));
static_assert(offsetof(GpuChunkVertex, position) == 0);
static_assert(offsetof(GpuChunkVertex, normal) == 12);
static_assert(offsetof(GpuChunkVertex, uv) == 24);
static_assert(offsetof(GpuChunkVertex, voxel_type) == 32);
static_assert(offsetof(GpuChunkVertex, light) == 34);
static_assert(offsetof(GpuChunkVertex, state_bits) == 36);

inline constexpr std::array<rhi::RenderVertexAttributeDesc, 6> gpu_chunk_vertex_attributes{
    rhi::RenderVertexAttributeDesc{0, 0, rhi::RenderVertexAttributeFormat::float3},
    rhi::RenderVertexAttributeDesc{1, 12, rhi::RenderVertexAttributeFormat::float3},
    rhi::RenderVertexAttributeDesc{2, 24, rhi::RenderVertexAttributeFormat::float2},
    rhi::RenderVertexAttributeDesc{3, 32, rhi::RenderVertexAttributeFormat::uint16},
    rhi::RenderVertexAttributeDesc{4, 34, rhi::RenderVertexAttributeFormat::uint8},
    rhi::RenderVertexAttributeDesc{5, 36, rhi::RenderVertexAttributeFormat::uint16},
};

[[nodiscard]] GpuChunkVertex to_gpu_chunk_vertex(const world::ChunkMeshVertex& vertex) noexcept;
[[nodiscard]] std::vector<GpuChunkVertex>
make_gpu_chunk_vertices(const std::vector<world::ChunkMeshVertex>& vertices);

} // namespace heartstead::renderer::terrain
