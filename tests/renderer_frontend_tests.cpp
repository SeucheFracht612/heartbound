#include "engine/profiling/cpu_timing.hpp"
#include "engine/renderer/camera/frustum.hpp"
#include "engine/renderer/chunks/chunk_gpu_cache.hpp"
#include "engine/renderer/chunks/chunk_render_system.hpp"
#include "engine/renderer/render_camera.hpp"
#include "engine/renderer/renderer.hpp"
#include "engine/renderer/rhi/render_device.hpp"
#include "engine/world/world_state.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <ranges>
#include <thread>

namespace {

void test_scoped_cpu_timing_zones() {
    heartstead::profiling::CpuTimingRecorder timings;
    {
        heartstead::profiling::ScopedCpuTimingZone zone(
            timings, heartstead::profiling::CpuTimingZone::meshing);
        std::uint64_t work = 0;
        for (std::uint64_t index = 0; index < 10'000; ++index) {
            work += index;
        }
        assert(work > 0);
    }
    assert(timings.milliseconds(heartstead::profiling::CpuTimingZone::meshing) > 0.0);
    assert(heartstead::profiling::cpu_timing_zone_name(
               heartstead::profiling::CpuTimingZone::visibility_culling) == "visibility_culling");
    timings.reset();
    assert(timings.milliseconds(heartstead::profiling::CpuTimingZone::meshing) == 0.0);
}

void test_frustum_aabb_culling() {
    heartstead::renderer::RenderCamera camera;
    camera.aspect_ratio = 1.0F;
    camera.near_plane = 0.1F;
    camera.far_plane = 100.0F;
    assert(camera.update_matrices());

    const auto frustum =
        heartstead::renderer::RenderFrustum::from_view_projection(camera.view_projection);
    assert(frustum.is_valid());
    assert(frustum.intersects({{-0.5F, -0.5F, -5.0F}, {0.5F, 0.5F, -4.0F}}));
    assert(!frustum.intersects({{-0.5F, -0.5F, 4.0F}, {0.5F, 0.5F, 5.0F}}));
    assert(!frustum.intersects({{200.0F, -0.5F, -5.0F}, {201.0F, 0.5F, -4.0F}}));
    assert(!frustum.intersects({{-0.5F, -0.5F, -101.0F}, {0.5F, 0.5F, -100.1F}}));

    // Invalid bounds are deliberately conservative while renderer data is being debugged.
    assert(frustum.intersects({{1.0F, 1.0F, 1.0F}, {-1.0F, -1.0F, -1.0F}}));
}

void test_chunk_gpu_cache_lifecycle() {
    using namespace heartstead;
    renderer::rhi::RenderDeviceDesc device_desc;
    device_desc.backend = renderer::rhi::RenderBackend::headless;
    device_desc.initial_extent = {640, 360};
    auto device = renderer::rhi::create_render_device(device_desc);
    assert(device);
    const auto baseline_resources = device.value()->live_resource_count();

    renderer::ChunkGpuCache cache(*device.value());
    assert(cache.initialize());
    const world::ChunkIdentity identity{{3, -2, 7}, 41};
    assert(cache.insert(identity));
    assert(!cache.insert(identity));
    assert(!cache.insert({identity.coordinate, 42}));

    const std::array<renderer::terrain::GpuChunkVertex, 3> vertices{
        renderer::terrain::GpuChunkVertex{{0.0F, 0.0F, 0.0F}},
        renderer::terrain::GpuChunkVertex{{1.0F, 0.0F, 0.0F}},
        renderer::terrain::GpuChunkVertex{{0.0F, 1.0F, 0.0F}},
    };
    const std::array<std::uint32_t, 3> indices{0, 1, 2};
    const math::Bounds3f bounds{{0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 0.0F}};
    auto first_upload = cache.replace_mesh(identity, 5, bounds, vertices, indices);
    assert(first_upload);
    assert(first_upload.value().uploaded_bytes == sizeof(vertices) + sizeof(indices));
    assert(device.value()->live_resource_count() == baseline_resources + 2);
    const auto* first_entry = cache.find(identity);
    assert(first_entry != nullptr);
    assert(first_entry->has_drawable_mesh());
    assert(first_entry->resident_content_revision == 5);
    const auto first_vertex_allocation = first_entry->mesh.vertices;
    const auto first_index_allocation = first_entry->mesh.indices;

    auto rejected_replacement = cache.replace_mesh(identity, 6, bounds, vertices, {});
    assert(!rejected_replacement);
    assert(cache.find(identity)->mesh.vertices == first_vertex_allocation);
    assert(cache.find(identity)->mesh.indices == first_index_allocation);
    assert(cache.find(identity)->resident_content_revision == 5);

    auto replacement = cache.replace_mesh(identity, 6, bounds, vertices, indices);
    assert(replacement);
    assert(replacement.value().replaced_resident_mesh);
    assert(device.value()->live_resource_count() == baseline_resources + 2);
    const auto* replaced_entry = cache.find(identity);
    assert(replaced_entry != nullptr);
    assert(replaced_entry->mesh.vertices != first_vertex_allocation);
    assert(replaced_entry->mesh.indices != first_index_allocation);
    assert(replaced_entry->resident_content_revision == 6);

    auto empty_replacement = cache.replace_mesh(identity, 7, {}, {}, {});
    assert(empty_replacement);
    assert(empty_replacement.value().empty);
    assert(device.value()->live_resource_count() == baseline_resources + 2);
    assert(cache.find(identity)->state == renderer::ChunkGpuState::resident);
    assert(!cache.find(identity)->has_drawable_mesh());
    assert(cache.stats().resident_chunk_count == 1);
    assert(cache.stats().empty_chunk_count == 1);
    assert(cache.stats().uploaded_chunk_count == 3);

    auto stale_erase = cache.erase({identity.coordinate, 40});
    assert(!stale_erase);
    assert(stale_erase.error().code == "chunk_gpu_cache.stale_load_generation");
    assert(cache.contains(identity));
    assert(cache.erase(identity));
    assert(cache.stats().entry_count == 0);
    assert(cache.stats().resident_allocation_count == 0);
    assert(cache.stats().vertex_arena.used_bytes == 0);
    assert(cache.stats().index_arena.used_bytes == 0);
    assert(device.value()->live_resource_count() == baseline_resources + 2);
    assert(cache.shutdown());
    assert(device.value()->live_resource_count() == baseline_resources);
}

void test_chunk_gpu_cache_batches_device_local_arena_uploads() {
    using namespace heartstead;
    renderer::rhi::RenderDeviceDesc device_desc;
    auto device = renderer::rhi::create_render_device(device_desc);
    assert(device);
    const auto baseline_resources = device.value()->live_resource_count();
    renderer::ChunkGpuCache cache(*device.value());
    renderer::ChunkGpuCacheConfig config;
    config.vertex_initial_bytes = 4096;
    config.vertex_maximum_bytes = 4096;
    config.index_initial_bytes = 4096;
    config.index_maximum_bytes = 4096;
    assert(cache.initialize(config));

    const world::ChunkIdentity first{{0, 0, 0}, 1};
    const world::ChunkIdentity second{{1, 0, 0}, 2};
    assert(cache.insert(first));
    assert(cache.insert(second));
    const std::array<renderer::terrain::GpuChunkVertex, 3> vertices{
        renderer::terrain::GpuChunkVertex{{0.0F, 0.0F, 0.0F}},
        renderer::terrain::GpuChunkVertex{{1.0F, 0.0F, 0.0F}},
        renderer::terrain::GpuChunkVertex{{0.0F, 1.0F, 0.0F}},
    };
    const std::array<std::uint32_t, 3> indices{0, 1, 2};
    const math::Bounds3f bounds{{0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 0.0F}};
    const std::array<renderer::ChunkGpuMeshUpload, 2> uploads{
        renderer::ChunkGpuMeshUpload{first, 1, 1, bounds, vertices, indices, {}},
        renderer::ChunkGpuMeshUpload{second, 1, 1, bounds, vertices, indices, {}},
    };
    auto uploaded = cache.replace_meshes(uploads);
    assert(uploaded);
    assert(uploaded.value().uploads.size() == 2);
    assert(uploaded.value().write_count == 4);
    assert(cache.stats().upload_batch_count == 1);
    assert(cache.stats().last_upload_chunk_count == 2);
    assert(cache.stats().last_upload_write_count == 4);
    assert(cache.stats().resident_allocation_count == 4);
    assert(cache.stats().resident_buffer_count == 2);
    assert(device.value()->live_resource_count() == baseline_resources + 2);
    assert(cache.find(first)->mesh.vertices.buffer == cache.find(second)->mesh.vertices.buffer);
    assert(cache.find(first)->mesh.indices.buffer == cache.find(second)->mesh.indices.buffer);
    assert(cache.find(first)->mesh.vertices.offset != cache.find(second)->mesh.vertices.offset);
    assert(cache.find(first)->mesh.indices.offset != cache.find(second)->mesh.indices.offset);

    assert(cache.erase(first));
    assert(cache.erase(second));
    assert(cache.stats().vertex_arena.used_bytes == 0);
    assert(cache.stats().index_arena.used_bytes == 0);
    assert(cache.shutdown());
    assert(device.value()->live_resource_count() == baseline_resources);
}

void test_chunk_render_system_retains_rebuilds_and_culls() {
    using namespace heartstead;
    renderer::rhi::RenderDeviceDesc device_desc;
    device_desc.backend = renderer::rhi::RenderBackend::headless;
    device_desc.initial_extent = {640, 640};
    auto device = renderer::rhi::create_render_device(device_desc);
    assert(device);
    const auto baseline_resources = device.value()->live_resource_count();

    renderer::ChunkGpuCache cache(*device.value());
    assert(cache.initialize());
    renderer::ChunkRenderConfig config;
    config.max_chunks_meshed_per_frame = 3;
    config.max_bytes_uploaded_per_frame = 1; // Oversized meshes make progress one at a time.
    renderer::ChunkRenderSystem chunks(cache, {9001}, nullptr, config);

    constexpr std::int64_t far = 1'000'000'000;
    const world::ChunkCoord origin_coord{far, 0, -far};
    const world::ChunkCoord neighbor_coord{far + 1, 0, -far};
    const world::ChunkCoord behind_coord{far, 0, -far + 3};
    world::WorldState world;
    assert(world.chunks().set(origin_coord, {31, 8, 31}, world::VoxelCell{1, 200}));
    assert(world.chunks().set(neighbor_coord, {0, 8, 31}, world::VoxelCell{1, 180}));
    assert(world.chunks().set(behind_coord, {0, 8, 0}, world::VoxelCell{1, 160}));

    const auto origin_identity = world.chunks().find(origin_coord)->identity();
    const auto neighbor_identity = world.chunks().find(neighbor_coord)->identity();
    const auto behind_identity = world.chunks().find(behind_coord)->identity();

    auto exact_origin = world::chunk_local_to_block(origin_coord, {0, 0, 0});
    assert(exact_origin);
    renderer::RenderCamera camera;
    camera.floating_origin.block = {exact_origin.value().x + 16, exact_origin.value().y + 16,
                                    exact_origin.value().z + 64};
    camera.aspect_ratio = 1.0F;
    camera.far_plane = 256.0F;
    assert(camera.update_matrices());

    const auto synchronize_until = [&](const auto& predicate) {
        for (std::size_t attempt = 0; attempt < 5'000; ++attempt) {
            assert(chunks.synchronize(world, camera));
            if (predicate()) {
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        assert(false && "asynchronous chunk renderer did not reach the expected state");
    };
    synchronize_until([&] {
        return chunks.stats().cache.resident_chunk_count == 3 &&
               chunks.stats().pending_mesh_count == 0 && chunks.stats().pending_upload_count == 0;
    });
    assert(cache.stats().resident_chunk_count == 3);
    assert(cache.stats().resident_buffer_count == 2);
    assert(cache.stats().resident_allocation_count == 6);
    assert(device.value()->live_resource_count() == baseline_resources + 2);
    assert(chunks.stats().pending_mesh_count == 0);
    assert(chunks.stats().pending_upload_count == 0);

    auto draws = chunks.build_draw_list(camera);
    assert(draws.visible_chunk_count == 2);
    assert(draws.culled_chunk_count == 1);
    assert(draws.draws.size() == 2);
    assert(draws.vertex_count > 0);
    assert(draws.index_count > 0);
    assert(std::ranges::all_of(draws.draws, [](const renderer::rhi::RenderDrawCommand& draw) {
        return draw.pipeline.value == 9001 && draw.vertex_buffer.is_valid() &&
               draw.index_buffer.is_valid() && draw.index_count > 0 &&
               std::abs(draw.camera_relative_origin.z + 64.0F) < 0.001F;
    }));
    assert(std::ranges::any_of(draws.draws, [](const renderer::rhi::RenderDrawCommand& draw) {
        return draw.first_index > 0 && draw.vertex_offset > 0;
    }));

    const auto uploads_before_idle_sync = cache.stats().uploaded_chunk_count;
    assert(chunks.synchronize(world, camera));
    assert(chunks.stats().uploaded_chunk_count == 0);
    assert(cache.stats().uploaded_chunk_count == uploads_before_idle_sync);

    const auto old_origin_vertex = cache.find(origin_identity)->mesh.vertices;
    const auto old_neighbor_vertex = cache.find(neighbor_identity)->mesh.vertices;
    const auto neighbor_revision = world.chunks().find(neighbor_coord)->content_revision();
    assert(world.chunks().set(origin_coord, {31, 8, 31}, world::VoxelCell{2, 220},
                              world.dirty_regions()));
    assert(chunks.synchronize(world, camera));
    assert(chunks.build_draw_list(camera).draws.size() == 2);
    assert(cache.find(origin_identity)->mesh.vertices == old_origin_vertex);
    synchronize_until([&] {
        return !world.chunks().find(origin_coord)->dirty().contains(world::ChunkDirtyFlag::mesh) &&
               !world.chunks().find(neighbor_coord)->dirty().contains(world::ChunkDirtyFlag::mesh);
    });
    assert(cache.find(origin_identity)->mesh.vertices != old_origin_vertex);
    assert(cache.find(neighbor_identity)->mesh.vertices != old_neighbor_vertex);
    assert(cache.find(origin_identity)->resident_content_revision ==
           world.chunks().find(origin_coord)->content_revision());
    assert(cache.find(neighbor_identity)->resident_content_revision == neighbor_revision);
    assert(!world.chunks().find(origin_coord)->dirty().contains(world::ChunkDirtyFlag::mesh));
    assert(!world.chunks().find(neighbor_coord)->dirty().contains(world::ChunkDirtyFlag::mesh));
    assert(device.value()->live_resource_count() == baseline_resources + 2);

    const auto intermediate_revision_before = world.chunks().find(origin_coord)->content_revision();
    assert(world.chunks().set(origin_coord, {30, 8, 31}, world::VoxelCell{1, 220},
                              world.dirty_regions()));
    const auto intermediate_revision = world.chunks().find(origin_coord)->content_revision();
    assert(intermediate_revision > intermediate_revision_before);
    assert(chunks.synchronize(world, camera)); // Queues the intermediate revision.
    assert(cache.find(origin_identity)->resident_content_revision != intermediate_revision);
    assert(world.chunks().set(origin_coord, {30, 8, 31}, world::VoxelCell{2, 220},
                              world.dirty_regions()));
    const auto newest_revision = world.chunks().find(origin_coord)->content_revision();
    assert(newest_revision > intermediate_revision);
    synchronize_until([&] {
        const auto* entry = cache.find(origin_identity);
        assert(entry != nullptr);
        assert(entry->resident_content_revision != intermediate_revision);
        return entry->resident_content_revision == newest_revision &&
               !world.chunks().find(origin_coord)->dirty().contains(world::ChunkDirtyFlag::mesh);
    });

    const world::ChunkCoord empty_coord{far, 3, -far};
    auto& empty = world.chunks().get_or_create(empty_coord);
    const auto empty_identity = empty.identity();
    synchronize_until([&] {
        const auto* entry = cache.find(empty_identity);
        return entry != nullptr && entry->state == renderer::ChunkGpuState::resident;
    });
    assert(cache.find(empty_identity) != nullptr);
    assert(cache.find(empty_identity)->state == renderer::ChunkGpuState::resident);
    assert(!cache.find(empty_identity)->has_drawable_mesh());
    assert(cache.stats().empty_chunk_count == 1);
    assert(device.value()->live_resource_count() == baseline_resources + 2);

    assert(world.chunks().erase(behind_coord));
    const std::array<world::ChunkIdentity, 1> evictions{behind_identity};
    assert(chunks.process_chunk_evictions(evictions));
    assert(!cache.contains(behind_identity));
    assert(device.value()->live_resource_count() == baseline_resources + 2);

    assert(world.chunks().set(empty_coord, {2, 2, 2}, world::VoxelCell{1, 255},
                              world.dirty_regions()));
    assert(chunks.synchronize(world, camera)); // Leaves work for the old generation in flight.
    assert(world.chunks().erase(empty_coord));
    auto& reloaded = world.chunks().get_or_create(empty_coord);
    assert(reloaded.set({1, 1, 1}, world::VoxelCell{3, 255}));
    assert(reloaded.identity() != empty_identity);
    assert(chunks.synchronize(world, camera));
    assert(!cache.contains(empty_identity));
    assert(cache.contains(reloaded.identity()));
    synchronize_until([&] {
        const auto* entry = cache.find(reloaded.identity());
        return entry != nullptr && entry->state == renderer::ChunkGpuState::resident &&
               entry->resident_content_revision == reloaded.content_revision();
    });
    assert(!cache.contains(empty_identity));

    assert(cache.clear());
    assert(device.value()->live_resource_count() == baseline_resources + 2);
    chunks.shutdown();
    assert(cache.shutdown());
    assert(device.value()->live_resource_count() == baseline_resources);
}

void test_renderer_frontend_submits_headless_frames() {
    using namespace heartstead;
    renderer::rhi::RenderDeviceDesc device_desc;
    device_desc.backend = renderer::rhi::RenderBackend::headless;
    device_desc.initial_extent = {640, 360};
    auto device = renderer::rhi::create_render_device(device_desc);
    assert(device);

    // Headless validates the SPIR-V header and all retained frame resources without invoking a
    // native shader compiler.
    const std::vector<std::uint32_t> test_spirv{0x07230203, 0x00010000, 0, 1, 0};
    renderer::RendererInitDesc init;
    init.device = std::move(device).value();
    init.terrain_vertex_spirv = test_spirv;
    init.terrain_fragment_spirv = test_spirv;
    init.chunk_config.max_chunks_meshed_per_frame = 2;
    init.chunk_config.max_bytes_uploaded_per_frame = 1024 * 1024;

    renderer::Renderer retained_renderer;
    assert(retained_renderer.initialize(std::move(init)));
    assert(retained_renderer.is_initialized());
    assert(retained_renderer.device() != nullptr);
    const auto initialized_resource_count = retained_renderer.device()->live_resource_count();
    assert(initialized_resource_count == 3); // two shader modules and one graphics pipeline

    world::WorldState world;
    assert(world.chunks().set({0, 0, 0}, {4, 4, 4}, world::VoxelCell{1, 255}));
    const auto identity = world.chunks().find({0, 0, 0})->identity();
    world::ChunkStreamLoadReport load_report;
    load_report.coord = identity.coordinate;
    load_report.identity = identity;
    const std::array<world::ChunkStreamLoadReport, 1> loads{load_report};
    assert(retained_renderer.process_chunk_loads(loads));
    renderer::RenderCamera camera;
    camera.floating_origin.block = {16, 16, 64};
    assert(camera.set_aspect_ratio(640.0F / 360.0F));

    for (std::size_t attempt = 0;
         attempt < 5'000 && retained_renderer.chunk_stats().cache.resident_chunk_count != 1;
         ++attempt) {
        assert(retained_renderer.synchronize_chunks(world, camera));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(retained_renderer.chunk_stats().cache.resident_chunk_count == 1);
    assert(retained_renderer.chunk_stats().cache.resident_buffer_count == 2);
    assert(retained_renderer.device()->live_resource_count() == initialized_resource_count + 2);

    auto first_frame = retained_renderer.render(camera);
    assert(first_frame);
    assert(first_frame.value().draw_count == 1);
    assert(first_frame.value().indexed_draw_count == 1);
    assert(first_frame.value().total_indices > 0);
    assert(retained_renderer.chunk_stats().visible_chunk_count == 1);
    assert(retained_renderer.chunk_stats().culled_chunk_count == 0);
    const auto& renderer_stats = retained_renderer.stats();
    assert(renderer_stats.cpu_frame_ms > 0.0);
    assert(renderer_stats.chunk_synchronization_ms > 0.0);
    assert(renderer_stats.meshing_ms > 0.0);
    assert(renderer_stats.upload_ms > 0.0);
    assert(renderer_stats.command_recording_ms > 0.0);
    assert(renderer_stats.loaded_chunks == 1);
    assert(renderer_stats.resident_chunks == 1);
    assert(renderer_stats.visible_chunks == 1);
    assert(renderer_stats.drawn_chunks == 1);
    assert(renderer_stats.draw_calls == 1);
    assert(renderer_stats.vertices > 0);
    assert(renderer_stats.triangles > 0);
    assert(renderer_stats.resident_mesh_bytes > 0);
    assert(renderer_stats.uploaded_bytes_this_frame > 0);
    assert(!renderer::format_renderer_stats(renderer_stats).empty());

    const auto uploaded_before_resize = retained_renderer.chunk_stats().cache.uploaded_chunk_count;
    assert(retained_renderer.resize({800, 400}));
    assert(camera.set_aspect_ratio(2.0F));
    assert(retained_renderer.synchronize_chunks(world, camera));
    assert(retained_renderer.chunk_stats().uploaded_chunk_count == 0);
    assert(retained_renderer.chunk_stats().cache.uploaded_chunk_count == uploaded_before_resize);
    assert(retained_renderer.device()->live_resource_count() == initialized_resource_count + 2);
    auto resized_frame = retained_renderer.render(camera);
    assert(resized_frame);
    assert(resized_frame.value().extent.width == 800);
    assert(resized_frame.value().extent.height == 400);
    assert(resized_frame.value().draw_count == 1);

    assert(world.chunks().erase(identity.coordinate));
    world::ChunkStreamEvictionReport eviction_report;
    eviction_report.evicted_chunks.push_back(identity.coordinate);
    eviction_report.evicted_identities.push_back(identity);
    assert(retained_renderer.process_chunk_evictions(eviction_report));
    assert(retained_renderer.device()->live_resource_count() == initialized_resource_count + 2);
    auto empty_frame = retained_renderer.render(camera);
    assert(empty_frame);
    assert(empty_frame.value().draw_count == 0);

    assert(retained_renderer.shutdown());
    assert(!retained_renderer.is_initialized());
    assert(retained_renderer.device() == nullptr);
}

} // namespace

int main() {
    test_scoped_cpu_timing_zones();
    test_frustum_aabb_culling();
    test_chunk_gpu_cache_lifecycle();
    test_chunk_gpu_cache_batches_device_local_arena_uploads();
    test_chunk_render_system_retains_rebuilds_and_culls();
    test_renderer_frontend_submits_headless_frames();
    return 0;
}
