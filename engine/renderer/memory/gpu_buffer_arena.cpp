#include "engine/renderer/memory/gpu_buffer_arena.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <ranges>
#include <utility>

namespace heartstead::renderer {

namespace {

[[nodiscard]] core::Result<std::uint64_t> align_up(std::uint64_t value, std::uint64_t alignment) {
    if (alignment == 0) {
        return core::Result<std::uint64_t>::failure("gpu_arena.invalid_alignment",
                                                    "GPU allocation alignment must be nonzero");
    }
    const auto remainder = value % alignment;
    if (remainder == 0) {
        return core::Result<std::uint64_t>::success(value);
    }
    const auto padding = alignment - remainder;
    if (value > std::numeric_limits<std::uint64_t>::max() - padding) {
        return core::Result<std::uint64_t>::failure("gpu_arena.alignment_overflow",
                                                    "GPU allocation alignment overflows uint64");
    }
    return core::Result<std::uint64_t>::success(value + padding);
}

} // namespace

bool GpuAllocation::is_valid() const noexcept {
    return buffer.is_valid() && size > 0 && generation > 0;
}

core::Status GpuBufferArenaConfig::validate() const {
    if (initial_block_bytes == 0 || maximum_capacity_bytes == 0 ||
        initial_block_bytes > maximum_capacity_bytes) {
        return core::Status::failure(
            "gpu_arena.invalid_capacity",
            "GPU arena capacities must be nonzero and initial capacity must fit the budget");
    }
    if (maximum_capacity_bytes > std::numeric_limits<std::size_t>::max()) {
        return core::Status::failure("gpu_arena.capacity_unsupported",
                                     "GPU arena capacity exceeds the host size_t range");
    }
    if (debug_name.empty()) {
        return core::Status::failure("gpu_arena.missing_debug_name",
                                     "GPU arena requires a debug name");
    }
    return core::Status::ok();
}

core::Result<std::unique_ptr<GpuBufferArena>> GpuBufferArena::create(rhi::IRenderDevice& device,
                                                                     GpuBufferArenaConfig config) {
    auto status = config.validate();
    if (!status) {
        return core::Result<std::unique_ptr<GpuBufferArena>>::failure(status.error().code,
                                                                      status.error().message);
    }
    return core::Result<std::unique_ptr<GpuBufferArena>>::success(
        std::unique_ptr<GpuBufferArena>(new GpuBufferArena(device, std::move(config))));
}

GpuBufferArena::GpuBufferArena(rhi::IRenderDevice& device, GpuBufferArenaConfig config)
    : device_(&device), config_(std::move(config)) {}

GpuBufferArena::~GpuBufferArena() {
    (void)shutdown();
}

core::Result<GpuAllocation> GpuBufferArena::allocate(std::uint64_t size, std::uint64_t alignment) {
    if (device_ == nullptr) {
        return core::Result<GpuAllocation>::failure("gpu_arena.stopped",
                                                    "GPU arena has been shut down");
    }
    if (size == 0 || alignment == 0) {
        return core::Result<GpuAllocation>::failure(
            "gpu_arena.invalid_allocation",
            "GPU allocation size and alignment must both be nonzero");
    }
    for (auto& block : blocks_) {
        for (std::size_t index = 0; index < block.free_ranges.size(); ++index) {
            const auto aligned = align_up(block.free_ranges[index].offset, alignment);
            if (!aligned) {
                return core::Result<GpuAllocation>::failure(aligned.error().code,
                                                            aligned.error().message);
            }
            const auto& range = block.free_ranges[index];
            if (aligned.value() >= range.offset && aligned.value() - range.offset <= range.size &&
                size <= range.size - (aligned.value() - range.offset)) {
                return allocate_from(block, index, size, alignment);
            }
        }
    }
    // New blocks begin at offset zero, which satisfies every nonzero alignment.
    auto block = grow(size);
    if (!block) {
        return core::Result<GpuAllocation>::failure(block.error().code, block.error().message);
    }
    return allocate_from(*block.value(), 0, size, alignment);
}

core::Status GpuBufferArena::retire(GpuAllocation allocation, std::uint64_t submission_serial) {
    auto* block = find_block(allocation.buffer);
    if (block == nullptr || !allocation.is_valid()) {
        return core::Status::failure("gpu_arena.unknown_allocation",
                                     "cannot retire an allocation outside this GPU arena");
    }
    const auto live = block->live_ranges.find(allocation.offset);
    if (live == block->live_ranges.end() || live->second.size != allocation.size ||
        live->second.generation != allocation.generation) {
        return core::Status::failure("gpu_arena.stale_allocation",
                                     "GPU allocation generation or range is stale");
    }
    if (live->second.retired) {
        return core::Status::failure("gpu_arena.duplicate_retirement",
                                     "GPU allocation has already been retired");
    }
    live->second.retired = true;
    retired_.push_back({allocation, submission_serial});
    refresh_stats();
    return core::Status::ok();
}

void GpuBufferArena::collect(std::uint64_t completed_submission_serial) noexcept {
    for (auto retired = retired_.begin(); retired != retired_.end();) {
        if (retired->retire_after_submission > completed_submission_serial) {
            ++retired;
            continue;
        }
        auto* block = find_block(retired->allocation.buffer);
        if (block != nullptr) {
            const auto live = block->live_ranges.find(retired->allocation.offset);
            if (live != block->live_ranges.end() &&
                live->second.generation == retired->allocation.generation) {
                insert_free_range(*block, {retired->allocation.offset, retired->allocation.size});
                block->live_ranges.erase(live);
            }
        }
        retired = retired_.erase(retired);
    }
    refresh_stats();
}

bool GpuBufferArena::owns(GpuAllocation allocation) const noexcept {
    const auto* block = find_block(allocation.buffer);
    if (block == nullptr || !allocation.is_valid()) {
        return false;
    }
    const auto live = block->live_ranges.find(allocation.offset);
    return live != block->live_ranges.end() && live->second.size == allocation.size &&
           live->second.generation == allocation.generation;
}

const GpuBufferArenaStats& GpuBufferArena::stats() noexcept {
    refresh_stats();
    return stats_;
}

core::Status GpuBufferArena::shutdown() {
    core::Status first_failure = core::Status::ok();
    if (device_ != nullptr) {
        for (const auto& block : blocks_) {
            auto status = device_->release_resource(block.handle);
            if (!status && first_failure) {
                first_failure = status;
            }
        }
    }
    blocks_.clear();
    retired_.clear();
    device_ = nullptr;
    refresh_stats();
    return first_failure;
}

core::Result<GpuBufferArena::Block*> GpuBufferArena::grow(std::uint64_t minimum_bytes) {
    const auto current_capacity = stats().capacity_bytes;
    if (current_capacity >= config_.maximum_capacity_bytes ||
        minimum_bytes > config_.maximum_capacity_bytes - current_capacity) {
        return core::Result<Block*>::failure("gpu_arena.budget_exhausted",
                                             "GPU arena memory budget is exhausted");
    }
    std::uint64_t desired =
        blocks_.empty() ? config_.initial_block_bytes : blocks_.back().capacity * 2U;
    if (!blocks_.empty() && desired < blocks_.back().capacity) {
        desired = config_.maximum_capacity_bytes - current_capacity;
    }
    desired = std::max(desired, minimum_bytes);
    desired = std::min(desired, config_.maximum_capacity_bytes - current_capacity);
    if (desired < minimum_bytes || desired > std::numeric_limits<std::size_t>::max()) {
        return core::Result<Block*>::failure("gpu_arena.budget_exhausted",
                                             "GPU arena cannot grow enough for the allocation");
    }

    rhi::RenderBufferDesc desc;
    desc.usage = config_.usage;
    desc.byte_size = static_cast<std::size_t>(desired);
    desc.debug_name = config_.debug_name + "_" + std::to_string(blocks_.size());
    desc.memory = rhi::RenderBufferMemory::device_local;
    auto created = device_->create_buffer(std::move(desc));
    if (!created) {
        return core::Result<Block*>::failure(created.error().code, created.error().message);
    }
    Block block;
    block.handle = created.value().handle;
    block.capacity = desired;
    block.free_ranges.push_back({0, desired});
    blocks_.push_back(std::move(block));
    ++stats_.growth_count;
    refresh_stats();
    return core::Result<Block*>::success(&blocks_.back());
}

core::Result<GpuAllocation> GpuBufferArena::allocate_from(Block& block, std::size_t range_index,
                                                          std::uint64_t size,
                                                          std::uint64_t alignment) {
    const auto range = block.free_ranges[range_index];
    const auto aligned = align_up(range.offset, alignment);
    if (!aligned || aligned.value() < range.offset || aligned.value() - range.offset > range.size ||
        size > range.size - (aligned.value() - range.offset)) {
        return core::Result<GpuAllocation>::failure("gpu_arena.range_too_small",
                                                    "GPU arena free range cannot fit allocation");
    }
    block.free_ranges.erase(block.free_ranges.begin() + static_cast<std::ptrdiff_t>(range_index));
    const auto prefix = aligned.value() - range.offset;
    const auto allocation_end = aligned.value() + size;
    const auto range_end = range.offset + range.size;
    if (prefix > 0) {
        insert_free_range(block, {range.offset, prefix});
    }
    if (allocation_end < range_end) {
        insert_free_range(block, {allocation_end, range_end - allocation_end});
    }
    if (next_generation_ == 0) {
        std::terminate();
    }
    const auto generation = next_generation_++;
    block.live_ranges.emplace(aligned.value(), LiveRange{size, generation, false});
    refresh_stats();
    return core::Result<GpuAllocation>::success(
        GpuAllocation{block.handle, aligned.value(), size, generation});
}

GpuBufferArena::Block* GpuBufferArena::find_block(rhi::RenderResourceHandle handle) noexcept {
    const auto found = std::ranges::find(blocks_, handle.value,
                                         [](const Block& block) { return block.handle.value; });
    return found == blocks_.end() ? nullptr : &*found;
}

const GpuBufferArena::Block*
GpuBufferArena::find_block(rhi::RenderResourceHandle handle) const noexcept {
    const auto found = std::ranges::find(blocks_, handle.value,
                                         [](const Block& block) { return block.handle.value; });
    return found == blocks_.end() ? nullptr : &*found;
}

void GpuBufferArena::insert_free_range(Block& block, FreeRange range) noexcept {
    if (range.size == 0) {
        return;
    }
    block.free_ranges.push_back(range);
    std::ranges::sort(block.free_ranges, {}, &FreeRange::offset);
    std::vector<FreeRange> merged;
    merged.reserve(block.free_ranges.size());
    for (const auto& candidate : block.free_ranges) {
        if (!merged.empty() && merged.back().offset + merged.back().size == candidate.offset) {
            merged.back().size += candidate.size;
        } else {
            merged.push_back(candidate);
        }
    }
    block.free_ranges = std::move(merged);
}

void GpuBufferArena::refresh_stats() noexcept {
    const auto growth_count = stats_.growth_count;
    stats_ = {};
    stats_.growth_count = growth_count;
    stats_.buffer_count = blocks_.size();
    stats_.retired_allocation_count = retired_.size();
    for (const auto& block : blocks_) {
        stats_.capacity_bytes += block.capacity;
        stats_.live_allocation_count += block.live_ranges.size();
        for (const auto& [_, live] : block.live_ranges) {
            stats_.used_bytes += live.size;
        }
        for (const auto& range : block.free_ranges) {
            stats_.free_bytes += range.size;
            stats_.largest_free_range_bytes = std::max(stats_.largest_free_range_bytes, range.size);
        }
    }
    if (stats_.free_bytes > 0) {
        stats_.fragmentation_ratio = 1.0 - static_cast<double>(stats_.largest_free_range_bytes) /
                                               static_cast<double>(stats_.free_bytes);
        stats_.fragmentation_ratio = std::clamp(stats_.fragmentation_ratio, 0.0, 1.0);
    }
}

} // namespace heartstead::renderer
