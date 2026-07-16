#include "engine/renderer/assets/shader_manager.hpp"
#include "engine/renderer/materials/pipeline_cache.hpp"
#include "engine/renderer/terrain/gpu_chunk_vertex.hpp"

#include <array>
#include <cassert>
#include <vector>

namespace {

using namespace heartstead;

[[nodiscard]] renderer::ShaderProgramDesc make_program(std::uint32_t bound) {
    const std::vector<std::uint32_t> spirv{0x07230203, 0x00010000, 0, bound, 0};
    renderer::ShaderProgramDesc program;
    program.id = "terrain";
    program.stages = {
        {renderer::rhi::RenderShaderStage::vertex, "main", spirv, "terrain.vert.spv"},
        {renderer::rhi::RenderShaderStage::fragment, "main", spirv, "terrain.frag.spv"},
    };
    program.interface.vertex_stride = sizeof(renderer::terrain::GpuChunkVertex);
    for (const auto& attribute : renderer::terrain::gpu_chunk_vertex_attributes) {
        program.interface.vertex_inputs.push_back({attribute.location, attribute.format});
    }
    program.interface.push_constant_ranges.push_back({renderer::rhi::RenderShaderStageFlags::vertex,
                                                      0,
                                                      sizeof(renderer::rhi::ChunkPushConstants)});
    program.dependencies = {"terrain.common", "camera.contract"};
    return program;
}

[[nodiscard]] renderer::rhi::RenderPipelineLayoutDesc make_layout() {
    auto material = core::PrototypeId::parse("base:materials/runtime_terrain");
    assert(material);
    renderer::rhi::RenderPipelineLayoutDesc layout;
    layout.material_id = material.value();
    layout.shader_template = {"base", "shaders/terrain.vert"};
    layout.push_constant_ranges.push_back({renderer::rhi::RenderShaderStageFlags::vertex, 0,
                                           sizeof(renderer::rhi::ChunkPushConstants)});
    layout.debug_name = "runtime_terrain_layout";
    return layout;
}

[[nodiscard]] renderer::rhi::RenderGraphicsPipelineDesc
make_pipeline(const renderer::rhi::RenderPipelineLayoutDesc& layout) {
    renderer::rhi::RenderGraphicsPipelineDesc pipeline;
    pipeline.material_id = layout.material_id;
    pipeline.debug_name = "runtime_terrain_pipeline";
    pipeline.vertex_stride = sizeof(renderer::terrain::GpuChunkVertex);
    pipeline.vertex_attributes.assign(renderer::terrain::gpu_chunk_vertex_attributes.begin(),
                                      renderer::terrain::gpu_chunk_vertex_attributes.end());
    pipeline.color_target_format = renderer::rhi::RenderImageFormat::rgba8_unorm;
    pipeline.depth_target_format = renderer::rhi::RenderImageFormat::d32_sfloat;
    return pipeline;
}

void test_shader_hot_reload_and_pipeline_cache() {
    renderer::rhi::RenderDeviceDesc device_desc;
    auto device = renderer::rhi::create_render_device(device_desc);
    assert(device);
    const auto baseline = device.value()->live_resource_count();

    renderer::ShaderManager shaders(*device.value(), true);
    auto program = shaders.create_program(make_program(1));
    assert(program);
    assert(shaders.stats().resident_program_count == 1);
    assert(shaders.stats().resident_module_count == 2);
    assert(shaders.find(program.value())->revision == 1);
    assert(device.value()->live_resource_count() == baseline + 2);

    renderer::PipelineCache pipelines(*device.value(), shaders);
    const auto layout = make_layout();
    const auto pipeline_desc = make_pipeline(layout);
    renderer::GraphicsPipelineKey key;
    key.shader_program = program.value();
    key.vertex_layout =
        renderer::hash_vertex_layout(pipeline_desc.vertex_stride, pipeline_desc.vertex_attributes);
    auto pipeline = pipelines.prewarm(key, layout, pipeline_desc);
    assert(pipeline);
    const auto first_pipeline = pipeline.value();
    assert(pipelines.prewarm(key, layout, pipeline_desc).value() == first_pipeline);
    assert(pipelines.stats().resident_pipeline_count == 1);
    assert(pipelines.stats().created_pipeline_count == 1);
    assert(device.value()->live_resource_count() == baseline + 3);

    pipelines.seal();
    auto missing_key = key;
    missing_key.feature_flags = 1;
    auto rejected = pipelines.prewarm(missing_key, layout, pipeline_desc);
    assert(!rejected);
    assert(rejected.error().code == "pipeline_cache.runtime_creation_rejected");
    assert(pipelines.stats().unexpected_creation_rejection_count == 1);

    auto invalid_reload = make_program(2);
    invalid_reload.stages[1].spirv[0] = 0;
    auto reload_status = shaders.reload_program(program.value(), std::move(invalid_reload));
    assert(!reload_status);
    assert(shaders.find(program.value())->revision == 1);
    assert(shaders.stats().failed_reload_count == 1);
    assert(device.value()->live_resource_count() == baseline + 3);

    assert(shaders.reload_program(program.value(), make_program(3)));
    assert(shaders.find(program.value())->revision == 2);
    assert(shaders.stats().successful_reload_count == 1);
    assert(shaders.stats().superseded_module_count == 2);
    assert(device.value()->live_resource_count() == baseline + 5);
    assert(pipelines.rebuild_program(program.value()));
    auto rebuilt = pipelines.find(key);
    assert(rebuilt);
    assert(rebuilt.value() != first_pipeline);
    assert(pipelines.stats().rebuilt_pipeline_count == 1);
    assert(device.value()->live_resource_count() == baseline + 3);
    assert(shaders.stats().superseded_module_count == 0);

    assert(pipelines.shutdown());
    assert(shaders.shutdown());
    assert(device.value()->live_resource_count() == baseline);
}

void test_shader_interface_rejects_mismatch() {
    renderer::rhi::RenderDeviceDesc device_desc;
    auto device = renderer::rhi::create_render_device(device_desc);
    assert(device);
    renderer::ShaderManager shaders(*device.value());
    auto program = shaders.create_program(make_program(1));
    assert(program);
    auto layout = make_layout();
    auto pipeline = make_pipeline(layout);
    pipeline.vertex_stride -= 4;
    auto status = renderer::validate_shader_interface(
        *shaders.find(program.value()), layout, pipeline.vertex_stride, pipeline.vertex_attributes);
    assert(!status);
    assert(status.error().code == "shader_manager.vertex_stride_mismatch");
    assert(shaders.shutdown());
}

} // namespace

int main() {
    test_shader_hot_reload_and_pipeline_cache();
    test_shader_interface_rejects_mismatch();
    return 0;
}
