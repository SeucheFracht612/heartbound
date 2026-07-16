#include "engine/renderer/memory/gpu_buffer_arena.hpp"
#include "engine/renderer/memory/staging_ring.hpp"
#include "engine/renderer/rhi/render_device.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

namespace {

void test_generation_safe_allocation_retirement_and_merging() {
    using namespace heartstead::renderer;
    rhi::RenderDeviceDesc device_desc;
    device_desc.backend = rhi::RenderBackend::headless;
    auto device_result = rhi::create_render_device(device_desc);
    assert(device_result);
    auto device = std::move(device_result).value();
    const auto baseline_resources = device->live_resource_count();

    GpuBufferArenaConfig config;
    config.usage = rhi::RenderBufferUsage::vertex;
    config.initial_block_bytes = 256;
    config.maximum_capacity_bytes = 256;
    config.debug_name = "arena_test";
    auto arena_result = GpuBufferArena::create(*device, config);
    assert(arena_result);
    auto arena = std::move(arena_result).value();

    auto first = arena->allocate(80, 64);
    auto second = arena->allocate(80, 64);
    assert(first && second);
    assert(first.value().offset == 0);
    assert(second.value().offset == 128);
    assert(first.value().buffer.value == second.value().buffer.value);
    assert(device->live_resource_count() == baseline_resources + 1);

    std::array<std::byte, 64> upload{};
    const std::array<rhi::RenderBufferWrite, 1> writes{
        rhi::RenderBufferWrite{first.value().buffer, static_cast<std::size_t>(first.value().offset),
                               upload},
    };
    auto uploaded = device->upload_buffer_batch(writes);
    assert(uploaded);
    assert(uploaded.value().write_count == 1);
    assert(uploaded.value().byte_size == upload.size());
    assert(uploaded.value().submission_serial == 1);
    assert(uploaded.value().cpu_gpu_wait_ms == 0.0);
    assert(device->last_submission_serial() == 1);
    assert(device->completed_submission_serial() == 1);

    const std::array<rhi::RenderBufferWrite, 1> invalid_writes{
        rhi::RenderBufferWrite{first.value().buffer, 240, upload},
    };
    auto invalid_upload = device->upload_buffer_batch(invalid_writes);
    assert(!invalid_upload);
    assert(invalid_upload.error().code == "renderer.buffer_write_out_of_bounds");

    assert(arena->retire(first.value(), 5));
    arena->collect(4);
    assert(arena->owns(first.value()));
    auto temporary = arena->allocate(32, 1);
    assert(temporary);
    assert(temporary.value().offset != first.value().offset);

    arena->collect(5);
    assert(!arena->owns(first.value()));
    auto replacement = arena->allocate(64, 64);
    assert(replacement);
    assert(replacement.value().offset == first.value().offset);
    assert(replacement.value().generation != first.value().generation);
    auto stale_retirement = arena->retire(first.value(), 6);
    assert(!stale_retirement);
    assert(stale_retirement.error().code == "gpu_arena.stale_allocation");

    assert(arena->retire(second.value(), 6));
    assert(arena->retire(temporary.value(), 6));
    assert(arena->retire(replacement.value(), 6));
    arena->collect(6);
    const auto stats = arena->stats();
    assert(stats.capacity_bytes == 256);
    assert(stats.used_bytes == 0);
    assert(stats.free_bytes == 256);
    assert(stats.largest_free_range_bytes == 256);
    assert(stats.fragmentation_ratio == 0.0);
    assert(stats.live_allocation_count == 0);
    assert(stats.retired_allocation_count == 0);

    auto exhausted = arena->allocate(257, 1);
    assert(!exhausted);
    assert(exhausted.error().code == "gpu_arena.budget_exhausted");
    assert(arena->shutdown());
    assert(device->live_resource_count() == baseline_resources);
}

void test_arena_growth_respects_budget() {
    using namespace heartstead::renderer;
    rhi::RenderDeviceDesc device_desc;
    auto device_result = rhi::create_render_device(device_desc);
    assert(device_result);
    auto device = std::move(device_result).value();

    GpuBufferArenaConfig config;
    config.usage = rhi::RenderBufferUsage::index;
    config.initial_block_bytes = 128;
    config.maximum_capacity_bytes = 512;
    config.debug_name = "growth_test";
    auto arena_result = GpuBufferArena::create(*device, config);
    assert(arena_result);
    auto arena = std::move(arena_result).value();
    auto first = arena->allocate(200, 1);
    auto second = arena->allocate(200, 1);
    assert(first && second);
    assert(first.value().buffer.value != second.value().buffer.value);
    assert(arena->stats().capacity_bytes == 512);
    assert(arena->stats().growth_count == 2);
    auto over_budget = arena->allocate(113, 1);
    assert(!over_budget);
    assert(over_budget.error().code == "gpu_arena.budget_exhausted");
}

void test_staging_ring_tracks_serials_and_wraps() {
    using namespace heartstead::renderer;
    StagingRingAllocator ring(64);
    auto first = ring.allocate(20, 4, 1, 0);
    auto second = ring.allocate(20, 4, 2, 0);
    assert(first && second);
    assert(first.value().offset == 0);
    assert(second.value().offset == 20);

    ring.release_completed(1);
    auto third = ring.allocate(16, 8, 3, 1);
    assert(third);
    assert(third.value().offset == 40);
    ring.release_completed(2);
    auto wrapped = ring.allocate(24, 8, 4, 2);
    assert(wrapped);
    assert(wrapped.value().offset == 0);
    assert(ring.stats().wrap_count == 1);
    assert(ring.stats().pending_range_count == 2);

    auto full = ring.allocate(32, 4, 5, 2);
    assert(!full);
    assert(full.error().code == "staging_ring.full");
    ring.release_completed(4);
    assert(ring.stats().used_bytes == 0);
    assert(ring.stats().free_bytes == 64);
    auto cancelled = ring.allocate(32, 4, 6, 4);
    assert(cancelled);
    assert(ring.cancel(cancelled.value()));
    assert(!ring.cancel(cancelled.value()));
    assert(ring.stats().used_bytes == 0);
}

} // namespace

int main() {
    test_generation_safe_allocation_retirement_and_merging();
    test_arena_growth_respects_budget();
    test_staging_ring_tracks_serials_and_wraps();
    return 0;
}
