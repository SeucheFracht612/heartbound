#include "engine/math/matrix.hpp"
#include "engine/renderer/render_camera.hpp"
#include "engine/renderer/shaders/spirv_loader.hpp"
#include "engine/renderer/terrain/gpu_chunk_vertex.hpp"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>

namespace {

[[nodiscard]] bool nearly_equal(float left, float right, float epsilon = 0.0001F) {
    return std::abs(left - right) <= epsilon;
}

void test_matrix_composition() {
    using namespace heartstead::math;

    const auto model = translation_matrix({5.0F, -2.0F, 3.0F}) *
                       scale_matrix({2.0F, 3.0F, 4.0F});
    const auto transformed = model * Vec4f{1.0F, 2.0F, -1.0F, 1.0F};
    assert(nearly_equal(transformed.x, 7.0F));
    assert(nearly_equal(transformed.y, 4.0F));
    assert(nearly_equal(transformed.z, -1.0F));
    assert(nearly_equal(transformed.w, 1.0F));

    const auto identity_composition = Mat4f::identity() * model;
    assert(identity_composition == model);
}

void test_vulkan_projection() {
    using namespace heartstead::math;

    constexpr float near_plane = 0.1F;
    constexpr float far_plane = 100.0F;
    const auto projection = perspective_projection(std::numbers::pi_v<float> * 0.5F, 2.0F,
                                                   near_plane, far_plane);
    const auto near_clip = projection * Vec4f{0.0F, 0.0F, -near_plane, 1.0F};
    const auto far_clip = projection * Vec4f{0.0F, 0.0F, -far_plane, 1.0F};
    assert(nearly_equal(near_clip.z / near_clip.w, 0.0F));
    assert(nearly_equal(far_clip.z / far_clip.w, 1.0F));

    const auto right_edge = projection * Vec4f{2.0F, 0.0F, -1.0F, 1.0F};
    assert(nearly_equal(right_edge.x / right_edge.w, 1.0F));
    const auto upper_edge = projection * Vec4f{0.0F, 1.0F, -1.0F, 1.0F};
    assert(nearly_equal(upper_edge.y / upper_edge.w, -1.0F));
}

void test_view_and_camera_resize() {
    using namespace heartstead;

    const auto view = math::view_matrix({3.0F, 4.0F, 5.0F}, 0.0F, 0.0F);
    const auto camera_origin = view * math::Vec4f{3.0F, 4.0F, 5.0F, 1.0F};
    assert(nearly_equal(camera_origin.x, 0.0F));
    assert(nearly_equal(camera_origin.y, 0.0F));
    assert(nearly_equal(camera_origin.z, 0.0F));

    renderer::RenderCamera camera;
    camera.local_position = {3.0F, 4.0F, 5.0F};
    assert(camera.update_matrices());
    const auto old_horizontal_scale = camera.projection.at(0, 0);
    assert(camera.set_aspect_ratio(4.0F / 3.0F));
    assert(camera.projection.at(0, 0) > old_horizontal_scale);
    assert(camera.view_projection == camera.projection * camera.view);
    assert(!camera.set_aspect_ratio(0.0F));
}

void test_gpu_chunk_vertex_contract() {
    using namespace heartstead;

    const world::ChunkMeshVertex cpu_vertex{{1.0F, 2.0F, 3.0F},
                                             {0.0F, 1.0F, 0.0F},
                                             0.25F,
                                             0.75F,
                                             42,
                                             190,
                                             0x1234};
    const auto gpu_vertex = renderer::terrain::to_gpu_chunk_vertex(cpu_vertex);
    assert(gpu_vertex.position[0] == 1.0F);
    assert(gpu_vertex.normal[1] == 1.0F);
    assert(gpu_vertex.uv[0] == 0.25F);
    assert(gpu_vertex.voxel_type == 42);
    assert(gpu_vertex.light == 190);
    assert(gpu_vertex.state_bits == 0x1234);
    assert(renderer::terrain::gpu_chunk_vertex_attributes[3].format ==
           renderer::rhi::RenderVertexAttributeFormat::uint16);
    assert(renderer::terrain::gpu_chunk_vertex_attributes[4].format ==
           renderer::rhi::RenderVertexAttributeFormat::uint8);
}

void test_spirv_validation() {
    using namespace heartstead::renderer::shaders;

    constexpr std::uint32_t valid_header[]{0x07230203, 0x00010000, 0, 1, 0};
    assert(validate_spirv(valid_header));
    constexpr std::uint32_t bad_magic[]{0xdeadbeef, 0x00010000, 0, 1, 0};
    const auto invalid = validate_spirv(bad_magic);
    assert(!invalid);
    assert(invalid.error().code == "renderer.invalid_spirv_magic");
}

} // namespace

int main() {
    test_matrix_composition();
    test_vulkan_projection();
    test_view_and_camera_resize();
    test_gpu_chunk_vertex_contract();
    test_spirv_validation();
    return 0;
}
