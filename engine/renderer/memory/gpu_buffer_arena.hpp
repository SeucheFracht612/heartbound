#pragma once

#include "engine/core/result.hpp"
#include "engine/renderer/rhi/render_device.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace heartstead::renderer {

struct GpuAllocation {
    rhi::RenderResourceHandle buffer;
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
    std::uint32_t generation = 0;

    [[nodiscard]] bool is_valid() const noexcept;
    friend auto operator<=>(const GpuAllocation&, const GpuAllocation&) = default;
};

struct GpuBufferArenaConfig {
    rhi::RenderBufferUsage usage = rhi::RenderBufferUsage::vertex;
    std::uint64_t initial_block_bytes = 16U * 1024U * 1024U;
    std::uint64_t maximum_capacity_bytes = 256U * 1024U * 1024U;
    std::string debug_name = "gpu_buffer_arena";

    [[nodiscard]] core::Status validate() const;
};

struct GpuBufferArenaStats {
    std::uint64_t capacity_bytes = 0;
    std::uint64_t used_bytes = 0;
    std::uint64_t free_bytes = 0;
    std::uint64_t largest_free_range_bytes = 0;
    double fragmentation_ratio = 0.0;
    std::size_t buffer_count = 0;
    std::size_t live_allocation_count = 0;
    std::size_t retired_allocation_count = 0;
    std::uint64_t growth_count = 0;
};

class GpuBufferArena {
  public:
    [[nodiscard]] static core::Result<std::unique_ptr<GpuBufferArena>>
    create(rhi::IRenderDevice& device, GpuBufferArenaConfig config);

    ~GpuBufferArena();

    GpuBufferArena(const GpuBufferArena&) = delete;
    GpuBufferArena& operator=(const GpuBufferArena&) = delete;

    [[nodiscard]] core::Result<GpuAllocation> allocate(std::uint64_t size, std::uint64_t alignment);
    [[nodiscard]] core::Status retire(GpuAllocation allocation, std::uint64_t submission_serial);
    void collect(std::uint64_t completed_submission_serial) noexcept;
    [[nodiscard]] bool owns(GpuAllocation allocation) const noexcept;
    [[nodiscard]] const GpuBufferArenaStats& stats() noexcept;
    [[nodiscard]] core::Status shutdown();

  private:
    struct FreeRange {
        std::uint64_t offset = 0;
        std::uint64_t size = 0;
    };

    struct LiveRange {
        std::uint64_t size = 0;
        std::uint32_t generation = 0;
        bool retired = false;
    };

    struct Block {
        rhi::RenderResourceHandle handle;
        std::uint64_t capacity = 0;
        std::vector<FreeRange> free_ranges;
        std::map<std::uint64_t, LiveRange> live_ranges;
    };

    struct RetiredAllocation {
        GpuAllocation allocation;
        std::uint64_t retire_after_submission = 0;
    };

    GpuBufferArena(rhi::IRenderDevice& device, GpuBufferArenaConfig config);

    [[nodiscard]] core::Result<Block*> grow(std::uint64_t minimum_bytes);
    [[nodiscard]] core::Result<GpuAllocation> allocate_from(Block& block, std::size_t range_index,
                                                            std::uint64_t size,
                                                            std::uint64_t alignment);
    [[nodiscard]] Block* find_block(rhi::RenderResourceHandle handle) noexcept;
    [[nodiscard]] const Block* find_block(rhi::RenderResourceHandle handle) const noexcept;
    static void insert_free_range(Block& block, FreeRange range) noexcept;
    void refresh_stats() noexcept;

    rhi::IRenderDevice* device_ = nullptr;
    GpuBufferArenaConfig config_{};
    std::vector<Block> blocks_;
    std::vector<RetiredAllocation> retired_;
    std::uint32_t next_generation_ = 1;
    GpuBufferArenaStats stats_{};
};

} // namespace heartstead::renderer
