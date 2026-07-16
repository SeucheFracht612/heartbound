#pragma once

#include "engine/core/result.hpp"
#include "engine/math/vector.hpp"
#include "engine/renderer/assets/render_asset_handles.hpp"
#include "engine/renderer/memory/gpu_buffer_arena.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace heartstead::renderer {

struct GpuStaticMeshVertex {
    float position[3]{};
    float normal[3]{};
    float uv[2]{};
};

static_assert(sizeof(GpuStaticMeshVertex) == 32);
static_assert(offsetof(GpuStaticMeshVertex, position) == 0);
static_assert(offsetof(GpuStaticMeshVertex, normal) == 12);
static_assert(offsetof(GpuStaticMeshVertex, uv) == 24);

inline constexpr rhi::RenderVertexAttributeDesc gpu_static_mesh_vertex_attributes[]{
    {0, offsetof(GpuStaticMeshVertex, position), rhi::RenderVertexAttributeFormat::float3},
    {1, offsetof(GpuStaticMeshVertex, normal), rhi::RenderVertexAttributeFormat::float3},
    {2, offsetof(GpuStaticMeshVertex, uv), rhi::RenderVertexAttributeFormat::float2},
};

struct StaticMeshUploadDesc {
    std::string id;
    std::span<const GpuStaticMeshVertex> vertices;
    std::span<const std::uint32_t> indices;
    math::Bounds3f local_bounds{};
};

struct RenderMeshView {
    RenderMeshHandle handle;
    std::string_view id;
    GpuAllocation vertices;
    GpuAllocation indices;
    std::uint32_t vertex_count = 0;
    std::uint32_t index_count = 0;
    rhi::RenderIndexType index_type = rhi::RenderIndexType::uint32;
    math::Bounds3f local_bounds{};
    bool fallback = false;
};

struct MeshManagerConfig {
    std::uint64_t vertex_initial_bytes = 4U * 1024U * 1024U;
    std::uint64_t vertex_maximum_bytes = 128U * 1024U * 1024U;
    std::uint64_t index_initial_bytes = 2U * 1024U * 1024U;
    std::uint64_t index_maximum_bytes = 64U * 1024U * 1024U;

    [[nodiscard]] core::Status validate() const;
};

struct MeshManagerStats {
    std::size_t resident_mesh_count = 0;
    std::uint64_t resident_mesh_bytes = 0;
    std::uint64_t uploaded_mesh_count = 0;
    std::uint64_t uploaded_bytes = 0;
    std::uint64_t fallback_resolution_count = 0;
    GpuBufferArenaStats vertex_arena;
    GpuBufferArenaStats index_arena;
};

class MeshManager {
  public:
    explicit MeshManager(rhi::IRenderDevice& device);
    ~MeshManager();

    MeshManager(const MeshManager&) = delete;
    MeshManager& operator=(const MeshManager&) = delete;

    [[nodiscard]] core::Status initialize(MeshManagerConfig config = {});
    [[nodiscard]] core::Result<RenderMeshHandle> create_mesh(const StaticMeshUploadDesc& desc);
    [[nodiscard]] core::Status release(RenderMeshHandle handle);
    [[nodiscard]] core::Status shutdown();

    // Missing/stale handles resolve visibly. Use find_exact() for ownership/lifetime checks.
    [[nodiscard]] const RenderMeshView* find(RenderMeshHandle handle) noexcept;
    [[nodiscard]] const RenderMeshView* find_exact(RenderMeshHandle handle) const noexcept;
    [[nodiscard]] const RenderMeshView* find(std::string_view id) const noexcept;
    [[nodiscard]] RenderMeshHandle fallback_mesh() const noexcept;
    [[nodiscard]] MeshManagerStats stats() noexcept;

  private:
    struct Record;

    [[nodiscard]] core::Result<RenderMeshHandle> upload_mesh(const StaticMeshUploadDesc& desc,
                                                             bool fallback);
    [[nodiscard]] Record* find_record(RenderMeshHandle handle) noexcept;
    [[nodiscard]] const Record* find_record(RenderMeshHandle handle) const noexcept;
    [[nodiscard]] core::Status retire_record(Record& record);
    void collect() noexcept;
    void refresh_stats() noexcept;

    rhi::IRenderDevice* device_ = nullptr;
    std::unique_ptr<GpuBufferArena> vertex_arena_;
    std::unique_ptr<GpuBufferArena> index_arena_;
    std::vector<Record> records_;
    RenderMeshHandle fallback_mesh_;
    MeshManagerStats stats_{};
};

[[nodiscard]] core::Status validate_static_mesh_upload(const StaticMeshUploadDesc& desc);

} // namespace heartstead::renderer
