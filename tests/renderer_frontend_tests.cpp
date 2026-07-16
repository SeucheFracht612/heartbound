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
#include <cmath>
#include <ranges>

namespace {

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
    const auto first_vertex_handle = first_entry->vertex_buffer.value;
    const auto first_index_handle = first_entry->index_buffer.value;

    auto rejected_replacement = cache.replace_mesh(identity, 6, bounds, vertices, {});
    assert(!rejected_replacement);
    assert(cache.find(identity)->vertex_buffer.value == first_vertex_handle);
    assert(cache.find(identity)->index_buffer.value == first_index_handle);
    assert(cache.find(identity)->resident_content_revision == 5);

    auto replacement = cache.replace_mesh(identity, 6, bounds, vertices, indices);
    assert(replacement);
    assert(replacement.value().replaced_resident_mesh);
    assert(device.value()->live_resource_count() == baseline_resources + 2);
    const auto* replaced_entry = cache.find(identity);
    assert(replaced_entry != nullptr);
    assert(replaced_entry->vertex_buffer.value != first_vertex_handle);
    assert(replaced_entry->index_buffer.value != first_index_handle);
    assert(replaced_entry->resident_content_revision == 6);

    auto empty_replacement = cache.replace_mesh(identity, 7, {}, {}, {});
    assert(empty_replacement);
    assert(empty_replacement.value().empty);
    assert(device.value()->live_resource_count() == baseline_resources);
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

    for (std::size_t frame = 0; frame < 8 && (chunks.stats().cache.resident_chunk_count < 3 ||
                                              chunks.stats().pending_mesh_count != 0 ||
                                              chunks.stats().pending_upload_count != 0);
         ++frame) {
        assert(chunks.synchronize(world, camera));
    }
    assert(cache.stats().resident_chunk_count == 3);
    assert(cache.stats().resident_buffer_count == 6);
    assert(device.value()->live_resource_count() == baseline_resources + 6);
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

    const auto uploads_before_idle_sync = cache.stats().uploaded_chunk_count;
    assert(chunks.synchronize(world, camera));
    assert(chunks.stats().uploaded_chunk_count == 0);
    assert(cache.stats().uploaded_chunk_count == uploads_before_idle_sync);

    const auto old_origin_vertex = cache.find(origin_identity)->vertex_buffer.value;
    const auto old_neighbor_vertex = cache.find(neighbor_identity)->vertex_buffer.value;
    const auto neighbor_revision = world.chunks().find(neighbor_coord)->content_revision();
    assert(world.chunks().set(origin_coord, {31, 8, 31}, world::VoxelCell{2, 220},
                              world.dirty_regions()));
    assert(chunks.synchronize(world, camera));
    assert(chunks.stats().pending_upload_count == 1);
    assert(chunks.build_draw_list(camera).draws.size() == 2);
    for (std::size_t frame = 0;
         frame < 6 &&
         (world.chunks().find(origin_coord)->dirty().contains(world::ChunkDirtyFlag::mesh) ||
          world.chunks().find(neighbor_coord)->dirty().contains(world::ChunkDirtyFlag::mesh));
         ++frame) {
        assert(chunks.synchronize(world, camera));
    }
    assert(cache.find(origin_identity)->vertex_buffer.value != old_origin_vertex);
    assert(cache.find(neighbor_identity)->vertex_buffer.value != old_neighbor_vertex);
    assert(cache.find(origin_identity)->resident_content_revision ==
           world.chunks().find(origin_coord)->content_revision());
    assert(cache.find(neighbor_identity)->resident_content_revision == neighbor_revision);
    assert(!world.chunks().find(origin_coord)->dirty().contains(world::ChunkDirtyFlag::mesh));
    assert(!world.chunks().find(neighbor_coord)->dirty().contains(world::ChunkDirtyFlag::mesh));
    assert(device.value()->live_resource_count() == baseline_resources + 6);

    const world::ChunkCoord empty_coord{far, 3, -far};
    auto& empty = world.chunks().get_or_create(empty_coord);
    const auto empty_identity = empty.identity();
    for (std::size_t frame = 0; frame < 4 && !cache.contains(empty_identity); ++frame) {
        assert(chunks.synchronize(world, camera));
    }
    assert(chunks.synchronize(world, camera));
    assert(cache.find(empty_identity) != nullptr);
    assert(cache.find(empty_identity)->state == renderer::ChunkGpuState::resident);
    assert(!cache.find(empty_identity)->has_drawable_mesh());
    assert(cache.stats().empty_chunk_count == 1);
    assert(device.value()->live_resource_count() == baseline_resources + 6);

    assert(world.chunks().erase(behind_coord));
    const std::array<world::ChunkIdentity, 1> evictions{behind_identity};
    assert(chunks.process_chunk_evictions(evictions));
    assert(!cache.contains(behind_identity));
    assert(device.value()->live_resource_count() == baseline_resources + 4);

    assert(world.chunks().erase(empty_coord));
    auto& reloaded = world.chunks().get_or_create(empty_coord);
    assert(reloaded.set({1, 1, 1}, world::VoxelCell{3, 255}));
    assert(reloaded.identity() != empty_identity);
    assert(chunks.synchronize(world, camera));
    assert(!cache.contains(empty_identity));
    assert(cache.contains(reloaded.identity()));

    assert(cache.clear());
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

    assert(retained_renderer.synchronize_chunks(world, camera));
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
    assert(retained_renderer.device()->live_resource_count() == initialized_resource_count);
    auto empty_frame = retained_renderer.render(camera);
    assert(empty_frame);
    assert(empty_frame.value().draw_count == 0);

    assert(retained_renderer.shutdown());
    assert(!retained_renderer.is_initialized());
    assert(retained_renderer.device() == nullptr);
}

} // namespace

int main() {
    test_frustum_aabb_culling();
    test_chunk_gpu_cache_lifecycle();
    test_chunk_render_system_retains_rebuilds_and_culls();
    test_renderer_frontend_submits_headless_frames();
    return 0;
}
