#include "engine/core/logging.hpp"
#include "engine/platform/platform.hpp"
#include "engine/renderer/materials/material_definition.hpp"
#include "engine/renderer/materials/material_pipeline_layout.hpp"
#include "engine/renderer/rhi/render_device.hpp"
#include "engine/renderer/rhi/render_frame_plan.hpp"
#include "engine/renderer/world_render_list.hpp"
#include "engine/world/meshing/chunk_mesher.hpp"
#include "engine/world/voxels/voxel_chunk.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>

int main() {
    using namespace heartstead;

    constexpr std::array<std::uint32_t, 35> minimal_compute_spirv{
        0x07230203, 0x00010000, 0x00000000, 0x00000006, 0x00000000, 0x00020011, 0x00000001,
        0x0003000e, 0x00000000, 0x00000001, 0x0005000f, 0x00000005, 0x00000004, 0x6e69616d,
        0x00000000, 0x00060010, 0x00000004, 0x00000011, 0x00000001, 0x00000001, 0x00000001,
        0x00020013, 0x00000001, 0x00030021, 0x00000002, 0x00000001, 0x00050036, 0x00000001,
        0x00000004, 0x00000000, 0x00000002, 0x000200f8, 0x00000005, 0x000100fd, 0x00010038,
    };
    constexpr std::array<std::uint32_t, 91> minimal_vertex_spirv{
        0x07230203, 0x00010000, 0x00000000, 0x00000011, 0x00000000, 0x00020011, 0x00000001,
        0x0003000e, 0x00000000, 0x00000001, 0x0006000f, 0x00000000, 0x0000000e, 0x6e69616d,
        0x00000000, 0x00000008, 0x00050048, 0x00000005, 0x00000000, 0x0000000b, 0x00000000,
        0x00030047, 0x00000005, 0x00000002, 0x00020013, 0x00000001, 0x00030021, 0x00000002,
        0x00000001, 0x00030016, 0x00000003, 0x00000020, 0x00040017, 0x00000004, 0x00000003,
        0x00000004, 0x0003001e, 0x00000005, 0x00000004, 0x00040020, 0x00000006, 0x00000003,
        0x00000005, 0x00040020, 0x00000007, 0x00000003, 0x00000004, 0x0004003b, 0x00000006,
        0x00000008, 0x00000003, 0x0004002b, 0x00000003, 0x00000009, 0x00000000, 0x0004002b,
        0x00000003, 0x0000000a, 0x3f800000, 0x00040015, 0x0000000b, 0x00000020, 0x00000001,
        0x0004002b, 0x0000000b, 0x0000000c, 0x00000000, 0x0007002c, 0x00000004, 0x0000000d,
        0x00000009, 0x00000009, 0x00000009, 0x0000000a, 0x00050036, 0x00000001, 0x0000000e,
        0x00000000, 0x00000002, 0x000200f8, 0x0000000f, 0x00050041, 0x00000007, 0x00000010,
        0x00000008, 0x0000000c, 0x0003003e, 0x00000010, 0x0000000d, 0x000100fd, 0x00010038,
    };
    constexpr std::array<std::uint32_t, 70> minimal_fragment_spirv{
        0x07230203, 0x00010000, 0x00000000, 0x0000000c, 0x00000000, 0x00020011, 0x00000001,
        0x0003000e, 0x00000000, 0x00000001, 0x0006000f, 0x00000004, 0x0000000a, 0x6e69616d,
        0x00000000, 0x00000006, 0x00030010, 0x0000000a, 0x00000007, 0x00040047, 0x00000006,
        0x0000001e, 0x00000000, 0x00020013, 0x00000001, 0x00030021, 0x00000002, 0x00000001,
        0x00030016, 0x00000003, 0x00000020, 0x00040017, 0x00000004, 0x00000003, 0x00000004,
        0x00040020, 0x00000005, 0x00000003, 0x00000004, 0x0004003b, 0x00000005, 0x00000006,
        0x00000003, 0x0004002b, 0x00000003, 0x00000007, 0x00000000, 0x0004002b, 0x00000003,
        0x00000008, 0x3f800000, 0x0007002c, 0x00000004, 0x00000009, 0x00000008, 0x00000007,
        0x00000008, 0x00000008, 0x00050036, 0x00000001, 0x0000000a, 0x00000000, 0x00000002,
        0x000200f8, 0x0000000b, 0x0003003e, 0x00000006, 0x00000009, 0x000100fd, 0x00010038,
    };
    constexpr std::array<std::uint32_t, 4> material_uniform_words{
        0x3f000000,
        0x3f666666,
        0x3f733333,
        0x3f800000,
    };
    constexpr std::array<std::uint8_t, 16> material_texture_pixels{
        255, 96, 64, 255, 64, 180, 255, 255, 96, 255, 128, 255, 255, 240, 96, 255,
    };

    core::log(core::LogLevel::info, "Heartstead renderer smoke starting");

    const auto vulkan_info =
        renderer::rhi::renderer_backend_info(renderer::rhi::RenderBackend::vulkan);
    core::log(core::LogLevel::info, "Vulkan backend status: " + std::string(vulkan_info.status));
    const auto vulkan_caps =
        renderer::rhi::render_backend_capabilities(renderer::rhi::RenderBackend::vulkan);
    core::log(
        core::LogLevel::info,
        "Vulkan contract: surface=" +
            std::string(vulkan_caps.requires_window_surface ? "required" : "not_required") +
            ", gpu=" + std::string(vulkan_caps.requires_gpu_device ? "required" : "not_required") +
            ", frames_in_flight=" + std::to_string(vulkan_caps.recommended_frames_in_flight));

    renderer::rhi::RenderDeviceDesc desc;
    desc.backend = vulkan_info.available ? renderer::rhi::RenderBackend::vulkan
                                         : renderer::rhi::RenderBackend::headless;
    desc.application_name = "Heartstead Renderer Smoke";
    desc.initial_extent = renderer::rhi::RenderExtent{1280, 720};

    std::unique_ptr<platform::IPlatform> native_platform;
    std::optional<platform::WindowId> native_window;
    if (desc.backend == renderer::rhi::RenderBackend::vulkan) {
        const auto native_info = platform::platform_backend_info(platform::PlatformBackend::native);
        if (native_info.available) {
            auto platform_result = platform::create_platform(
                platform::PlatformDesc{platform::PlatformBackend::native});
            if (platform_result) {
                native_platform = std::move(platform_result).value();
                auto window = native_platform->create_window(
                    platform::WindowDesc{"Heartstead Renderer Smoke", 1280, 720, true});
                if (window) {
                    native_window = window.value();
                    auto handle = native_platform->native_window_handle(native_window.value());
                    if (handle) {
                        desc.native_window = handle.value();
                        core::log(core::LogLevel::info,
                                  "Renderer smoke will request Vulkan presentation");
                    }
                }
            }
        }
    }

    auto device = renderer::rhi::create_render_device(desc);
    if (!device) {
        core::log(core::LogLevel::error, device.error().message);
        return 1;
    }
    const auto capabilities = device.value()->capabilities();
    const auto request_present = capabilities.supports_present;
    core::log(
        core::LogLevel::info,
        "Renderer capabilities: present=" +
            std::string(capabilities.supports_present ? "yes" : "no") +
            ", validation=" + std::string(capabilities.supports_validation ? "yes" : "no") +
            ", shader_modules=" + std::string(capabilities.supports_shader_modules ? "yes" : "no") +
            ", debug_markers=" + std::string(capabilities.supports_debug_markers ? "yes" : "no") +
            ", max_extent=" + std::to_string(capabilities.max_extent.width) + "x" +
            std::to_string(capabilities.max_extent.height));

    auto shader_module = device.value()->create_shader_module(
        renderer::rhi::RenderShaderModuleDesc{renderer::rhi::RenderShaderStage::compute,
                                              "minimal_compute_shader"},
        minimal_compute_spirv);
    if (!shader_module) {
        core::log(core::LogLevel::error, shader_module.error().message);
        return 1;
    }
    core::log(
        core::LogLevel::info,
        "Created " +
            std::string(renderer::rhi::render_shader_stage_name(shader_module.value().stage)) +
            " shader module with " + std::to_string(shader_module.value().word_count) +
            " SPIR-V word(s), gpu_backed=" +
            std::string(shader_module.value().gpu_backed ? "yes" : "no"));
    auto material_id = core::PrototypeId::parse("base:materials/debug_smoke");
    auto shader_template = assets::VirtualPath::parse("base:shaders/templates/surface.slang");
    auto albedo_texture = assets::VirtualPath::parse("base:textures/debug/smoke_albedo.ktx2");
    if (!material_id || !shader_template || !albedo_texture) {
        core::log(core::LogLevel::error, "Failed to create renderer smoke material ids");
        return 1;
    }

    renderer::materials::MaterialDefinition debug_material;
    debug_material.id = material_id.value();
    debug_material.domain = renderer::materials::MaterialDomain::surface;
    debug_material.blend_mode = renderer::materials::MaterialBlendMode::opaque;
    debug_material.shader_template = shader_template.value();
    debug_material.textures.push_back({"albedo", albedo_texture.value(), true});
    debug_material.scalars.push_back({"roughness", 0.5F});
    debug_material.colors.push_back(
        {"tint", renderer::materials::MaterialColor{0.9F, 0.95F, 1.0F, 1.0F}});

    renderer::materials::MaterialRegistry material_registry;
    auto material_status = material_registry.add(debug_material);
    if (!material_status) {
        core::log(core::LogLevel::error, material_status.error().message);
        return 1;
    }
    core::log(core::LogLevel::info,
              "Registered " + std::to_string(material_registry.size()) +
                  " material definition(s) for " +
                  std::string(renderer::materials::material_domain_name(debug_material.domain)));

    world::VoxelChunk debug_chunk({0, 0, 0});
    auto voxel_status = debug_chunk.set({0, 0, 0}, world::VoxelCell{1, 15});
    if (!voxel_status) {
        core::log(core::LogLevel::error, voxel_status.error().message);
        return 1;
    }
    auto chunk_mesh = world::ChunkMesher::build_surface_mesh(debug_chunk);
    if (!chunk_mesh) {
        core::log(core::LogLevel::error, chunk_mesh.error().message);
        return 1;
    }

    const auto vertex_bytes = std::as_bytes(std::span(chunk_mesh.value().vertices));
    auto vertex_upload = device.value()->upload_buffer(
        renderer::rhi::RenderBufferDesc{renderer::rhi::RenderBufferUsage::vertex,
                                        vertex_bytes.size(), "debug_chunk_vertices"},
        vertex_bytes);
    if (!vertex_upload) {
        core::log(core::LogLevel::error, vertex_upload.error().message);
        return 1;
    }

    const auto index_bytes = std::as_bytes(std::span(chunk_mesh.value().indices));
    auto index_upload = device.value()->upload_buffer(
        renderer::rhi::RenderBufferDesc{renderer::rhi::RenderBufferUsage::index, index_bytes.size(),
                                        "debug_chunk_indices"},
        index_bytes);
    if (!index_upload) {
        core::log(core::LogLevel::error, index_upload.error().message);
        return 1;
    }

    core::log(
        core::LogLevel::info,
        "Uploaded chunk mesh buffers: vertices=" + std::to_string(vertex_upload.value().byte_size) +
            " bytes, indices=" + std::to_string(index_upload.value().byte_size) +
            " bytes, gpu_backed=" + std::string(vertex_upload.value().gpu_backed ? "yes" : "no"));

    auto pipeline_layout =
        renderer::materials::render_pipeline_layout_from_material(debug_material);
    if (!pipeline_layout) {
        core::log(core::LogLevel::error, pipeline_layout.error().message);
        return 1;
    }
    auto pipeline_stats = device.value()->bind_pipeline_layout(pipeline_layout.value());
    if (!pipeline_stats) {
        core::log(core::LogLevel::error, pipeline_stats.error().message);
        return 1;
    }
    core::log(core::LogLevel::info,
              "Bound material pipeline layout with " +
                  std::to_string(pipeline_stats.value().descriptor_count) +
                  " descriptor(s), gpu_backed=" +
                  std::string(pipeline_stats.value().gpu_backed ? "yes" : "no"));

    const auto uniform_bytes = std::as_bytes(std::span(material_uniform_words));
    auto uniform_upload = device.value()->upload_buffer(
        renderer::rhi::RenderBufferDesc{renderer::rhi::RenderBufferUsage::uniform,
                                        uniform_bytes.size(), "debug_material_uniforms"},
        uniform_bytes);
    if (!uniform_upload) {
        core::log(core::LogLevel::error, uniform_upload.error().message);
        return 1;
    }
    const auto texture_bytes = std::as_bytes(std::span(material_texture_pixels));
    auto texture_upload = device.value()->upload_image(
        renderer::rhi::RenderImageDesc{renderer::rhi::RenderImageFormat::rgba8_unorm, 2, 2,
                                       "debug_material_albedo"},
        texture_bytes);
    if (!texture_upload) {
        core::log(core::LogLevel::error, texture_upload.error().message);
        return 1;
    }
    const std::array<renderer::rhi::RenderDescriptorWrite, 3> material_writes{
        renderer::rhi::RenderDescriptorWrite{
            debug_material.id,
            "roughness",
            uniform_upload.value().handle,
            0,
            uniform_bytes.size(),
        },
        renderer::rhi::RenderDescriptorWrite{
            debug_material.id,
            "tint",
            uniform_upload.value().handle,
            0,
            uniform_bytes.size(),
        },
        renderer::rhi::RenderDescriptorWrite{
            debug_material.id,
            "albedo",
            texture_upload.value().handle,
        },
    };
    auto descriptor_write =
        device.value()->write_descriptors(std::span<const renderer::rhi::RenderDescriptorWrite>{
            material_writes.data(), material_writes.size()});
    if (!descriptor_write) {
        core::log(core::LogLevel::error, descriptor_write.error().message);
        return 1;
    }
    core::log(core::LogLevel::info,
              "Wrote " + std::to_string(descriptor_write.value().write_count) +
                  " material descriptor(s), gpu_backed=" +
                  std::string(descriptor_write.value().gpu_backed ? "yes" : "no"));

    auto vertex_shader_module = device.value()->create_shader_module(
        renderer::rhi::RenderShaderModuleDesc{renderer::rhi::RenderShaderStage::vertex,
                                              "minimal_vertex_shader"},
        minimal_vertex_spirv);
    if (!vertex_shader_module) {
        core::log(core::LogLevel::error, vertex_shader_module.error().message);
        return 1;
    }
    auto fragment_shader_module = device.value()->create_shader_module(
        renderer::rhi::RenderShaderModuleDesc{renderer::rhi::RenderShaderStage::fragment,
                                              "minimal_fragment_shader"},
        minimal_fragment_spirv);
    if (!fragment_shader_module) {
        core::log(core::LogLevel::error, fragment_shader_module.error().message);
        return 1;
    }
    renderer::rhi::RenderGraphicsPipelineDesc graphics_desc{vertex_shader_module.value().handle,
                                                            fragment_shader_module.value().handle,
                                                            debug_material.id,
                                                            "main",
                                                            "main",
                                                            "minimal_graphics_pipeline"};
    graphics_desc.vertex_stride = sizeof(world::ChunkMeshVertex);
    graphics_desc.vertex_attributes = {
        {0, offsetof(world::ChunkMeshVertex, position),
         renderer::rhi::RenderVertexAttributeFormat::float3},
        {1, offsetof(world::ChunkMeshVertex, normal),
         renderer::rhi::RenderVertexAttributeFormat::float3},
        {2, offsetof(world::ChunkMeshVertex, u),
         renderer::rhi::RenderVertexAttributeFormat::float2},
        {3, offsetof(world::ChunkMeshVertex, voxel_type),
         renderer::rhi::RenderVertexAttributeFormat::uint16},
        {4, offsetof(world::ChunkMeshVertex, light),
         renderer::rhi::RenderVertexAttributeFormat::uint8},
        {5, offsetof(world::ChunkMeshVertex, state_bits),
         renderer::rhi::RenderVertexAttributeFormat::uint16},
    };
    auto graphics_pipeline = device.value()->create_graphics_pipeline(graphics_desc);
    if (!graphics_pipeline) {
        core::log(core::LogLevel::error, graphics_pipeline.error().message);
        return 1;
    }
    core::log(core::LogLevel::info,
              "Created graphics pipeline, gpu_backed=" +
                  std::string(graphics_pipeline.value().gpu_backed ? "yes" : "no"));

    auto compute_pipeline =
        device.value()->create_compute_pipeline(renderer::rhi::RenderComputePipelineDesc{
            shader_module.value().handle,
            debug_material.id,
            "main",
            "minimal_compute_pipeline",
        });
    if (!compute_pipeline) {
        core::log(core::LogLevel::error, compute_pipeline.error().message);
        return 1;
    }
    core::log(core::LogLevel::info,
              "Created compute pipeline, gpu_backed=" +
                  std::string(compute_pipeline.value().gpu_backed ? "yes" : "no"));
    auto pipeline_release = device.value()->release_resource(compute_pipeline.value().handle);
    if (!pipeline_release) {
        core::log(core::LogLevel::error, pipeline_release.error().message);
        return 1;
    }
    auto shader_release = device.value()->release_resource(shader_module.value().handle);
    if (!shader_release) {
        core::log(core::LogLevel::error, shader_release.error().message);
        return 1;
    }

    renderer::rhi::RenderMeshBinding chunk_draw{
        vertex_upload.value().handle,
        index_upload.value().handle,
        debug_material.id,
        static_cast<std::uint32_t>(chunk_mesh.value().vertices.size()),
        static_cast<std::uint32_t>(chunk_mesh.value().indices.size()),
        1,
        "debug_chunk_draw",
    };
    world::WorldPosition chunk_position;
    chunk_position.anchor = {debug_chunk.coord().x * world::VoxelChunk::edge_length,
                             debug_chunk.coord().y * world::VoxelChunk::edge_length,
                             debug_chunk.coord().z * world::VoxelChunk::edge_length};
    auto anchor_status = renderer::anchor_mesh_draw(chunk_draw, chunk_position, {{0, 0, 0}});
    if (!anchor_status) {
        core::log(core::LogLevel::error, anchor_status.error().message);
        return 1;
    }
    auto draw_stats =
        device.value()->bind_mesh_draws(std::span<const renderer::rhi::RenderMeshBinding>{
            &chunk_draw,
            1,
        });
    if (!draw_stats) {
        core::log(core::LogLevel::error, draw_stats.error().message);
        return 1;
    }
    core::log(core::LogLevel::info,
              "Bound " + std::to_string(draw_stats.value().draw_count) + " mesh draw(s) with " +
                  std::to_string(draw_stats.value().material_count) + " material(s), submitted=" +
                  std::string(draw_stats.value().draw_commands_submitted ? "yes" : "no"));

    renderer::rhi::RenderFramePlanBuilder planned_frame(desc.initial_extent);
    auto plan_status = planned_frame.add_resource(
        {"swapchain", desc.initial_extent, renderer::rhi::RenderResourceLifetime::external});
    if (!plan_status) {
        core::log(core::LogLevel::error, plan_status.error().message);
        return 1;
    }
    plan_status = planned_frame.add_resource(
        {"scene_color", desc.initial_extent, renderer::rhi::RenderResourceLifetime::transient});
    if (!plan_status) {
        core::log(core::LogLevel::error, plan_status.error().message);
        return 1;
    }
    plan_status = planned_frame.add_pass({"clear_scene",
                                          renderer::rhi::RenderPassKind::clear,
                                          {},
                                          {"scene_color"},
                                          renderer::rhi::ClearColor{0.08F, 0.12F, 0.16F, 1.0F},
                                          false});
    if (!plan_status) {
        core::log(core::LogLevel::error, plan_status.error().message);
        return 1;
    }
    plan_status = planned_frame.add_pass({"world",
                                          renderer::rhi::RenderPassKind::world,
                                          {"scene_color"},
                                          {"scene_color"},
                                          {},
                                          false});
    if (!plan_status) {
        core::log(core::LogLevel::error, plan_status.error().message);
        return 1;
    }
    plan_status = planned_frame.add_pass({"post",
                                          renderer::rhi::RenderPassKind::post_process,
                                          {"scene_color"},
                                          {"swapchain"},
                                          {},
                                          false});
    if (!plan_status) {
        core::log(core::LogLevel::error, plan_status.error().message);
        return 1;
    }
    if (request_present) {
        plan_status = planned_frame.add_pass(
            {"present", renderer::rhi::RenderPassKind::present, {"swapchain"}, {}, {}, true});
        if (!plan_status) {
            core::log(core::LogLevel::error, plan_status.error().message);
            return 1;
        }
    }
    auto plan = planned_frame.build();
    if (!plan) {
        core::log(core::LogLevel::error, plan.error().message);
        return 1;
    }
    if (device.value()->backend() == renderer::rhi::RenderBackend::vulkan) {
        auto unsupported_stats = device.value()->execute_frame_plan(plan.value());
        if (unsupported_stats ||
            unsupported_stats.error().code != "renderer.vulkan_unsupported_frame_plan") {
            core::log(core::LogLevel::error,
                      "Vulkan accepted a frame plan containing operations it does not implement");
            return 1;
        }
        core::log(core::LogLevel::info,
                  "Rejected an unsupported generic Vulkan frame plan as expected");

        auto supported_plan = renderer::rhi::make_clear_present_frame_plan(
            desc.initial_extent, renderer::rhi::ClearColor{0.08F, 0.12F, 0.16F, 1.0F},
            request_present);
        auto planned_stats = device.value()->execute_frame_plan(supported_plan);
        if (!planned_stats) {
            core::log(core::LogLevel::error, planned_stats.error().message);
            return 1;
        }
        core::log(core::LogLevel::info, "Executed supported Vulkan frame plan with " +
                                            std::to_string(
                                                planned_stats.value().render_pass_count) +
                                            " pass(es)");
    } else {
        auto planned_stats = device.value()->execute_frame_plan(plan.value());
        if (!planned_stats) {
            core::log(core::LogLevel::error, planned_stats.error().message);
            return 1;
        }
        core::log(core::LogLevel::info, "Executed renderer frame plan with " +
                                            std::to_string(
                                                planned_stats.value().render_pass_count) +
                                            " pass(es)");
    }

    for (std::uint32_t frame = 0; frame < 3; ++frame) {
        const auto brightness = static_cast<float>(frame) * 0.25F;
        auto stats = device.value()->render_frame(renderer::rhi::RenderFrameDesc{
            renderer::rhi::ClearColor{0.10F + brightness, 0.18F, 0.24F, 1.0F},
            {},
            request_present,
        });
        if (!stats) {
            core::log(core::LogLevel::error, stats.error().message);
            return 1;
        }

        core::log(core::LogLevel::info,
                  "Rendered smoke frame " + std::to_string(stats.value().frame_index) +
                      " through " + std::string(device.value()->backend_name()) + " at " +
                      std::to_string(stats.value().extent.width) + "x" +
                      std::to_string(stats.value().extent.height) + " with " +
                      std::to_string(stats.value().render_pass_count) + " validated pass(es)");
    }

    const auto release_graphics_pipeline =
        device.value()->release_resource(graphics_pipeline.value().handle);
    const auto release_vertex_shader =
        device.value()->release_resource(vertex_shader_module.value().handle);
    const auto release_fragment_shader =
        device.value()->release_resource(fragment_shader_module.value().handle);
    const auto release_uniform = device.value()->release_resource(uniform_upload.value().handle);
    const auto release_texture = device.value()->release_resource(texture_upload.value().handle);
    const auto release_vertices = device.value()->release_resource(vertex_upload.value().handle);
    const auto release_indices = device.value()->release_resource(index_upload.value().handle);
    if (!release_graphics_pipeline || !release_vertex_shader || !release_fragment_shader ||
        !release_uniform || !release_texture || !release_vertices || !release_indices) {
        core::log(core::LogLevel::error, "Failed to release renderer smoke resources");
        return 1;
    }

    core::log(core::LogLevel::info, "Renderer smoke completed " +
                                        std::to_string(device.value()->completed_frame_count()) +
                                        " frame(s)");
    return 0;
}
