#include "engine/renderer/camera/frustum.hpp"
#include "engine/renderer/chunks/chunk_gpu_cache.hpp"
#include "engine/renderer/render_camera.hpp"
#include "engine/renderer/rhi/render_device.hpp"

#include <array>
#include <cassert>

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

} // namespace

int main() {
    test_frustum_aabb_culling();
    test_chunk_gpu_cache_lifecycle();
    return 0;
}
