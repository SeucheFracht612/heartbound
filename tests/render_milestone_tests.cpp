#include "engine/math/matrix.hpp"
#include "engine/renderer/render_camera.hpp"
#include "engine/renderer/rhi/render_frame_plan.hpp"
#include "engine/renderer/shaders/spirv_loader.hpp"
#include "engine/renderer/terrain/gpu_chunk_vertex.hpp"

#include <cassert>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <span>

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

void test_unified_headless_frame_submission() {
    using namespace heartstead;
    using namespace renderer::rhi;

    auto device = create_render_device(RenderDeviceDesc{});
    assert(device);
    const auto material = core::PrototypeId::parse("base:materials/terrain_test");
    assert(material);

    RenderPipelineLayoutDesc layout;
    layout.material_id = material.value();
    layout.shader_template = {"base", "shaders/terrain_test.glsl"};
    layout.push_constant_ranges.push_back(
        {RenderShaderStageFlags::vertex, 0, sizeof(ChunkPushConstants)});
    assert(device.value()->bind_pipeline_layout(layout));

    constexpr std::array<std::uint32_t, 5> spirv_header{
        0x07230203, 0x00010000, 0, 1, 0,
    };
    auto vertex_shader = device.value()->create_shader_module(
        {RenderShaderStage::vertex, "headless_terrain_vertex"}, spirv_header);
    auto fragment_shader = device.value()->create_shader_module(
        {RenderShaderStage::fragment, "headless_terrain_fragment"}, spirv_header);
    assert(vertex_shader);
    assert(fragment_shader);

    RenderGraphicsPipelineDesc pipeline_desc;
    pipeline_desc.vertex_shader = vertex_shader.value().handle;
    pipeline_desc.fragment_shader = fragment_shader.value().handle;
    pipeline_desc.material_id = material.value();
    pipeline_desc.vertex_stride = sizeof(renderer::terrain::GpuChunkVertex);
    pipeline_desc.vertex_attributes.assign(renderer::terrain::gpu_chunk_vertex_attributes.begin(),
                                           renderer::terrain::gpu_chunk_vertex_attributes.end());
    auto pipeline = device.value()->create_graphics_pipeline(pipeline_desc);
    assert(pipeline);

    constexpr std::array<renderer::terrain::GpuChunkVertex, 3> vertices{};
    constexpr std::array<std::uint32_t, 3> indices{0, 1, 2};
    auto vertex_upload = device.value()->upload_buffer(
        {RenderBufferUsage::vertex, sizeof(vertices), "headless_terrain_vertices"},
        std::as_bytes(std::span(vertices)));
    auto index_upload = device.value()->upload_buffer(
        {RenderBufferUsage::index, sizeof(indices), "headless_terrain_indices"},
        std::as_bytes(std::span(indices)));
    assert(vertex_upload);
    assert(index_upload);

    RenderFramePlanBuilder builder({640, 360});
    assert(builder.add_resource(
        {"output", {640, 360}, RenderResourceLifetime::external,
         RenderImageFormat::rgba8_unorm}));
    assert(builder.add_resource(
        {"depth", {640, 360}, RenderResourceLifetime::transient,
         RenderImageFormat::d32_sfloat}));
    assert(builder.add_pass({"world",
                             RenderPassKind::world,
                             {},
                             {"output", "depth"},
                             {0.05F, 0.1F, 0.2F, 1.0F},
                             false}));
    assert(builder.add_pass(
        {"present", RenderPassKind::present, {"output"}, {}, {}, true}));
    auto plan = builder.build();
    assert(plan);

    RenderFrameSubmission frame;
    frame.plan = plan.value();
    frame.camera.view_projection = math::Mat4f::identity();
    frame.pass_commands.push_back(
        {0,
         {RenderDrawCommand{pipeline.value().handle,
                            vertex_upload.value().handle,
                            index_upload.value().handle,
                            3,
                            0,
                            0,
                            1,
                            0,
                            {32.0F, 0.0F, -32.0F}}}});

    auto stats = device.value()->execute_frame(frame);
    assert(stats);
    assert(stats.value().draw_count == 1);
    assert(stats.value().indexed_draw_count == 1);
    assert(stats.value().total_indices == 3);
    assert(stats.value().presented);

    frame.pass_commands.front().draws.front().first_index = 1;
    auto out_of_bounds = device.value()->execute_frame(frame);
    assert(!out_of_bounds);
    assert(out_of_bounds.error().code == "renderer.draw_index_range_out_of_bounds");
}

} // namespace

int main() {
    test_matrix_composition();
    test_vulkan_projection();
    test_view_and_camera_resize();
    test_gpu_chunk_vertex_contract();
    test_spirv_validation();
    test_unified_headless_frame_submission();
    return 0;
}
