#include "engine/core/logging.hpp"
#include "engine/platform/platform.hpp"
#include "engine/renderer/render_camera.hpp"
#include "engine/renderer/rhi/render_device.hpp"
#include "engine/renderer/rhi/render_frame_plan.hpp"
#include "engine/renderer/shaders/spirv_loader.hpp"
#include "engine/renderer/terrain/gpu_chunk_vertex.hpp"
#include "engine/world/meshing/chunk_mesher.hpp"
#include "engine/world/voxels/voxel_chunk.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <thread>

namespace {

using namespace heartstead;

[[nodiscard]] core::Result<world::VoxelChunk> make_test_chunk() {
    // A deliberately far-away chunk proves that only origin-relative deltas reach the GPU.
    world::VoxelChunk chunk({1'000'000'000, 0, -1'000'000'000});
    for (std::uint16_t z = 0; z < world::VoxelChunk::edge_length; ++z) {
        for (std::uint16_t x = 0; x < world::VoxelChunk::edge_length; ++x) {
            const auto wave = 2.5F * std::sin(static_cast<float>(x) * 0.27F) +
                              2.0F * std::cos(static_cast<float>(z) * 0.23F);
            const auto height = static_cast<std::uint16_t>(
                std::clamp(7 + static_cast<int>(std::lround(wave)), 3, 13));
            for (std::uint16_t y = 0; y <= height; ++y) {
                const std::uint16_t type = y == height ? 1 : (y + 3 >= height ? 2 : 3);
                auto status = chunk.set({x, y, z}, world::VoxelCell{type, 255, 0, 0});
                if (!status) {
                    return core::Result<world::VoxelChunk>::failure(status.error().code,
                                                                    status.error().message);
                }
            }
        }
    }
    return core::Result<world::VoxelChunk>::success(std::move(chunk));
}

[[nodiscard]] core::Result<renderer::rhi::RenderFramePlan>
make_terrain_frame_plan(renderer::rhi::RenderExtent extent) {
    using namespace renderer::rhi;
    RenderFramePlanBuilder builder(extent);
    auto status = builder.add_resource(
        {"output", extent, RenderResourceLifetime::external, RenderImageFormat::rgba8_unorm});
    if (!status) {
        return core::Result<RenderFramePlan>::failure(status.error().code, status.error().message);
    }
    status = builder.add_resource(
        {"depth", extent, RenderResourceLifetime::transient, RenderImageFormat::d32_sfloat});
    if (!status) {
        return core::Result<RenderFramePlan>::failure(status.error().code, status.error().message);
    }
    status = builder.add_pass({"world",
                               RenderPassKind::world,
                               {},
                               {"output", "depth"},
                               ClearColor{0.055F, 0.09F, 0.14F, 1.0F},
                               false});
    if (!status) {
        return core::Result<RenderFramePlan>::failure(status.error().code, status.error().message);
    }
    status = builder.add_pass({"present", RenderPassKind::present, {"output"}, {}, {}, true});
    if (!status) {
        return core::Result<RenderFramePlan>::failure(status.error().code, status.error().message);
    }
    return builder.build();
}

[[nodiscard]] int fail(std::string_view message) {
    core::log(core::LogLevel::error, message);
    return 1;
}

} // namespace

int main() {
    using namespace heartstead;
    using namespace renderer::rhi;

    core::log(core::LogLevel::info, "Heartstead terrain renderer starting");
    const auto native_info = platform::platform_backend_info(platform::PlatformBackend::native);
    if (!native_info.available) {
        return fail("A native X11 display is required: " + std::string(native_info.status));
    }
    const auto vulkan_info = renderer_backend_info(RenderBackend::vulkan);
    if (!vulkan_info.available) {
        return fail("A Vulkan graphics device is required: " + std::string(vulkan_info.status));
    }

    auto active_platform = platform::create_platform({platform::PlatformBackend::native});
    if (!active_platform) {
        return fail(active_platform.error().message);
    }
    constexpr RenderExtent initial_extent{1280, 720};
    auto window = active_platform.value()->create_window(
        {"Heartstead - Milestone 1 Terrain", initial_extent.width, initial_extent.height, true});
    if (!window) {
        return fail(window.error().message);
    }
    auto native_handle = active_platform.value()->native_window_handle(window.value());
    if (!native_handle) {
        return fail("Native platform did not expose a Vulkan window handle");
    }

    RenderDeviceDesc device_desc;
    device_desc.backend = RenderBackend::vulkan;
    device_desc.application_name = "Heartstead Terrain Renderer";
    device_desc.initial_extent = initial_extent;
    device_desc.present_mode = PresentMode::fifo;
    device_desc.enable_validation = true;
    device_desc.native_window = native_handle.value();
    auto device = create_render_device(device_desc);
    if (!device) {
        return fail(device.error().message);
    }
    const auto device_capabilities = device.value()->capabilities();
    core::log(core::LogLevel::info,
              "Vulkan validation active: " +
                  std::string(device_capabilities.supports_validation ? "yes" : "no"));

    auto test_chunk = make_test_chunk();
    if (!test_chunk) {
        return fail(test_chunk.error().message);
    }
    auto chunk_mesh = world::ChunkMesher::build_surface_mesh(test_chunk.value());
    if (!chunk_mesh) {
        return fail(chunk_mesh.error().message);
    }
    if (chunk_mesh.value().empty() ||
        chunk_mesh.value().indices.size() > std::numeric_limits<std::uint32_t>::max()) {
        return fail("Generated terrain chunk did not produce a renderable uint32 mesh");
    }
    auto gpu_vertices = renderer::terrain::make_gpu_chunk_vertices(chunk_mesh.value().vertices);
    const auto vertex_bytes = std::as_bytes(std::span(gpu_vertices));
    const auto index_bytes = std::as_bytes(std::span(chunk_mesh.value().indices));
    auto vertex_upload = device.value()->upload_buffer(
        {RenderBufferUsage::vertex, vertex_bytes.size(), "milestone_terrain_vertices"},
        vertex_bytes);
    auto index_upload = device.value()->upload_buffer(
        {RenderBufferUsage::index, index_bytes.size(), "milestone_terrain_indices"}, index_bytes);
    if (!vertex_upload) {
        return fail(vertex_upload.error().message);
    }
    if (!index_upload) {
        return fail(index_upload.error().message);
    }

    const std::filesystem::path shader_root =
        std::filesystem::path{HEARTSTEAD_RENDER_SMOKE_ASSET_DIR} / "shaders";
    auto vertex_spirv = renderer::shaders::load_spirv_file(shader_root / "terrain.vert.spv");
    auto fragment_spirv = renderer::shaders::load_spirv_file(shader_root / "terrain.frag.spv");
    if (!vertex_spirv) {
        return fail("Vertex shader loading failed visibly: " + vertex_spirv.error().message);
    }
    if (!fragment_spirv) {
        return fail("Fragment shader loading failed visibly: " + fragment_spirv.error().message);
    }

    const auto terrain_material = core::PrototypeId::parse("base:materials/milestone_terrain");
    if (!terrain_material) {
        return fail("Internal terrain material id is invalid");
    }
    RenderPipelineLayoutDesc pipeline_layout;
    pipeline_layout.material_id = terrain_material.value();
    pipeline_layout.shader_template = {"base", "shaders/terrain.vert"};
    pipeline_layout.push_constant_ranges.push_back(
        {RenderShaderStageFlags::vertex, 0, sizeof(ChunkPushConstants)});
    pipeline_layout.debug_name = "milestone_terrain_layout";
    auto layout_stats = device.value()->bind_pipeline_layout(pipeline_layout);
    if (!layout_stats) {
        return fail(layout_stats.error().message);
    }

    auto vertex_shader = device.value()->create_shader_module(
        {RenderShaderStage::vertex, "milestone_terrain_vertex"}, vertex_spirv.value());
    auto fragment_shader = device.value()->create_shader_module(
        {RenderShaderStage::fragment, "milestone_terrain_fragment"}, fragment_spirv.value());
    if (!vertex_shader) {
        return fail(vertex_shader.error().message);
    }
    if (!fragment_shader) {
        return fail(fragment_shader.error().message);
    }

    RenderGraphicsPipelineDesc graphics_pipeline_desc;
    graphics_pipeline_desc.vertex_shader = vertex_shader.value().handle;
    graphics_pipeline_desc.fragment_shader = fragment_shader.value().handle;
    graphics_pipeline_desc.material_id = terrain_material.value();
    graphics_pipeline_desc.debug_name = "milestone_terrain_pipeline";
    graphics_pipeline_desc.vertex_stride = sizeof(renderer::terrain::GpuChunkVertex);
    graphics_pipeline_desc.vertex_attributes.assign(
        renderer::terrain::gpu_chunk_vertex_attributes.begin(),
        renderer::terrain::gpu_chunk_vertex_attributes.end());
    graphics_pipeline_desc.topology = RenderPrimitiveTopology::triangle_list;
    graphics_pipeline_desc.polygon_mode = RenderPolygonMode::fill;
    graphics_pipeline_desc.cull_mode = RenderCullMode::back;
    graphics_pipeline_desc.front_face = RenderFrontFace::counter_clockwise;
    graphics_pipeline_desc.depth_test_enable = true;
    graphics_pipeline_desc.depth_write_enable = true;
    graphics_pipeline_desc.depth_compare = RenderCompareOperation::less;
    graphics_pipeline_desc.blend_mode = RenderBlendMode::disabled;
    graphics_pipeline_desc.color_target_format = RenderImageFormat::rgba8_unorm;
    graphics_pipeline_desc.depth_target_format = RenderImageFormat::d32_sfloat;
    auto graphics_pipeline = device.value()->create_graphics_pipeline(graphics_pipeline_desc);
    if (!graphics_pipeline) {
        return fail(graphics_pipeline.error().message);
    }

    const auto chunk_origin_x =
        test_chunk.value().coord().x * static_cast<std::int64_t>(world::VoxelChunk::edge_length);
    const auto chunk_origin_y =
        test_chunk.value().coord().y * static_cast<std::int64_t>(world::VoxelChunk::edge_length);
    const auto chunk_origin_z =
        test_chunk.value().coord().z * static_cast<std::int64_t>(world::VoxelChunk::edge_length);
    renderer::RenderCamera camera;
    camera.floating_origin.block = {chunk_origin_x + 16, chunk_origin_y, chunk_origin_z + 48};
    camera.local_position = {0.0F, 18.0F, 0.0F};
    camera.pitch_radians = -0.34F;
    if (auto status = camera.set_aspect_ratio(static_cast<float>(initial_extent.width) /
                                              static_cast<float>(initial_extent.height));
        !status) {
        return fail(status.error().message);
    }
    world::WorldPosition chunk_world_position;
    chunk_world_position.anchor = {chunk_origin_x, chunk_origin_y, chunk_origin_z};
    auto chunk_relative_origin =
        world::to_camera_relative(chunk_world_position, camera.floating_origin);
    if (!chunk_relative_origin) {
        return fail(chunk_relative_origin.error().message);
    }

    auto frame_plan = make_terrain_frame_plan(initial_extent);
    if (!frame_plan) {
        return fail(frame_plan.error().message);
    }
    RenderFrameSubmission frame;
    frame.plan = frame_plan.value();
    frame.pass_commands.push_back(
        {0,
         {RenderDrawCommand{
             graphics_pipeline.value().handle,
             vertex_upload.value().handle,
             index_upload.value().handle,
             static_cast<std::uint32_t>(chunk_mesh.value().indices.size()),
             0,
             0,
             1,
             0,
             chunk_relative_origin.value(),
         }}});

    core::log(core::LogLevel::info,
              "Terrain ready: " + std::to_string(chunk_mesh.value().face_count) +
                  " faces. Controls: WASD move, Space rises, hold right mouse to look, Esc exits.");

    RenderExtent framebuffer_extent = initial_extent;
    auto previous_time = std::chrono::steady_clock::now();
    bool have_last_mouse = false;
    std::int32_t last_mouse_x = 0;
    std::int32_t last_mouse_y = 0;
    std::uint64_t rendered_frames = 0;
    while (!active_platform.value()->should_quit()) {
        active_platform.value()->begin_frame();
        while (auto event = active_platform.value()->poll_event()) {
            if (event->kind == platform::PlatformEventKind::quit_requested ||
                event->kind == platform::PlatformEventKind::window_closed) {
                active_platform.value()->request_quit();
            } else if (event->kind == platform::PlatformEventKind::window_resized &&
                       event->window_id == window.value()) {
                framebuffer_extent = {event->width, event->height};
                if (framebuffer_extent.is_valid()) {
                    auto resize_status = device.value()->resize(framebuffer_extent);
                    if (!resize_status) {
                        return fail(resize_status.error().message);
                    }
                    auto camera_status =
                        camera.set_aspect_ratio(static_cast<float>(framebuffer_extent.width) /
                                                static_cast<float>(framebuffer_extent.height));
                    if (!camera_status) {
                        return fail(camera_status.error().message);
                    }
                    frame_plan = make_terrain_frame_plan(framebuffer_extent);
                    if (!frame_plan) {
                        return fail(frame_plan.error().message);
                    }
                    frame.plan = frame_plan.value();
                    core::log(core::LogLevel::info,
                              "Renderer resized to " + std::to_string(framebuffer_extent.width) +
                                  "x" + std::to_string(framebuffer_extent.height));
                }
            }
        }
        if (active_platform.value()->should_quit()) {
            break;
        }
        if (active_platform.value()->was_key_pressed(window.value(), platform::KeyCode::escape)) {
            active_platform.value()->request_quit();
            continue;
        }

        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = std::chrono::duration<float>(now - previous_time).count();
        previous_time = now;
        const auto delta_seconds = std::clamp(elapsed, 0.0F, 0.1F);
        constexpr float movement_speed = 12.0F;
        auto planar_forward = camera.forward();
        planar_forward.y = 0.0F;
        const auto planar_length = static_cast<float>(math::length(planar_forward));
        if (planar_length > 0.0F) {
            planar_forward /= planar_length;
        }
        if (active_platform.value()->is_key_down(window.value(), platform::KeyCode::w)) {
            camera.local_position += planar_forward * (movement_speed * delta_seconds);
        }
        if (active_platform.value()->is_key_down(window.value(), platform::KeyCode::s)) {
            camera.local_position -= planar_forward * (movement_speed * delta_seconds);
        }
        if (active_platform.value()->is_key_down(window.value(), platform::KeyCode::d)) {
            camera.local_position += camera.right() * (movement_speed * delta_seconds);
        }
        if (active_platform.value()->is_key_down(window.value(), platform::KeyCode::a)) {
            camera.local_position -= camera.right() * (movement_speed * delta_seconds);
        }
        if (active_platform.value()->is_key_down(window.value(), platform::KeyCode::space)) {
            camera.local_position.y += movement_speed * delta_seconds;
        }

        const auto input = active_platform.value()->input_snapshot(window.value());
        if (input && active_platform.value()->is_mouse_button_down(window.value(),
                                                                   platform::MouseButton::right)) {
            if (have_last_mouse) {
                constexpr float look_sensitivity = 0.004F;
                camera.yaw_radians +=
                    static_cast<float>(input->mouse.x - last_mouse_x) * look_sensitivity;
                camera.pitch_radians = std::clamp(
                    camera.pitch_radians -
                        static_cast<float>(input->mouse.y - last_mouse_y) * look_sensitivity,
                    -1.45F, 1.45F);
            }
            have_last_mouse = true;
            last_mouse_x = input->mouse.x;
            last_mouse_y = input->mouse.y;
        } else {
            have_last_mouse = false;
        }

        if (!framebuffer_extent.is_valid()) {
            // A minimized X11 window is represented by a zero framebuffer extent. Keep pumping
            // native events without touching the swapchain or spinning a CPU core.
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }
        auto camera_status = camera.update_matrices();
        if (!camera_status) {
            return fail(camera_status.error().message);
        }
        frame.camera.view_projection = camera.view_projection;
        auto stats = device.value()->execute_frame(frame);
        if (!stats) {
            return fail(stats.error().message);
        }
        ++rendered_frames;
        if (rendered_frames <= 3) {
            core::log(core::LogLevel::info, "Unified terrain frame " +
                                                std::to_string(rendered_frames) +
                                                " presented successfully");
        }
        // Keep FIFO presentation from being saturated by this intentionally single-frame MVP.
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    core::log(core::LogLevel::info, "Stopping terrain renderer and releasing GPU resources");
    const auto release_pipeline =
        device.value()->release_resource(graphics_pipeline.value().handle);
    const auto release_vertex_shader =
        device.value()->release_resource(vertex_shader.value().handle);
    const auto release_fragment_shader =
        device.value()->release_resource(fragment_shader.value().handle);
    const auto release_vertices = device.value()->release_resource(vertex_upload.value().handle);
    const auto release_indices = device.value()->release_resource(index_upload.value().handle);
    if (!release_pipeline || !release_vertex_shader || !release_fragment_shader ||
        !release_vertices || !release_indices) {
        return fail("Failed to release one or more terrain renderer resources");
    }
    core::log(core::LogLevel::debug, "Terrain GPU resources released");
    device.value().reset();
    core::log(core::LogLevel::debug, "Vulkan render device destroyed");
    if (const auto* state = active_platform.value()->find_window(window.value());
        state != nullptr && state->open) {
        const auto close_status = active_platform.value()->close_window(window.value());
        if (!close_status) {
            return fail(close_status.error().message);
        }
    }
    core::log(core::LogLevel::info, "Heartstead terrain renderer shut down cleanly");
    return 0;
}
