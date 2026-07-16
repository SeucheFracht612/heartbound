#include "engine/renderer/renderer.hpp"

#include "engine/renderer/terrain/gpu_chunk_vertex.hpp"

#include <algorithm>
#include <chrono>
#include <limits>
#include <utility>

namespace heartstead::renderer {

namespace {

const ChunkRenderStats empty_chunk_stats{};

} // namespace

Renderer::~Renderer() {
    (void)shutdown();
}

core::Status Renderer::initialize(RendererInitDesc desc) {
    if (is_initialized()) {
        return core::Status::failure("renderer.already_initialized",
                                     "renderer cannot be initialized twice");
    }
    if (desc.device == nullptr) {
        return core::Status::failure("renderer.missing_device",
                                     "renderer initialization requires a render device");
    }
    auto config_status = desc.chunk_config.validate();
    if (!config_status) {
        return config_status;
    }
    config_status = desc.chunk_gpu_cache_config.validate();
    if (!config_status) {
        return config_status;
    }

    device_ = std::move(desc.device);
    auto pipeline_status =
        create_terrain_pipeline(desc.terrain_vertex_spirv, desc.terrain_fragment_spirv);
    if (!pipeline_status) {
        const auto error = pipeline_status.error();
        (void)shutdown();
        return core::Status::failure(error.code, error.message);
    }

    chunk_cache_ = std::make_unique<ChunkGpuCache>(*device_);
    auto cache_status = chunk_cache_->initialize(desc.chunk_gpu_cache_config);
    if (!cache_status) {
        const auto error = cache_status.error();
        (void)shutdown();
        return core::Status::failure(error.code, error.message);
    }
    chunk_system_ = std::make_unique<ChunkRenderSystem>(*chunk_cache_, terrain_pipeline_,
                                                        desc.voxel_palette, desc.chunk_config);
    auto chunk_system_status = chunk_system_->initialize();
    if (!chunk_system_status) {
        const auto error = chunk_system_status.error();
        (void)shutdown();
        return core::Status::failure(error.code, error.message);
    }
    frame_builder_ = std::make_unique<FrameBuilder>(device_->current_extent(), desc.clear_color);
    return core::Status::ok();
}

core::Status Renderer::shutdown() {
    core::Status first_failure = core::Status::ok();
    draw_command_scratch_.clear();
    frame_builder_.reset();
    chunk_system_.reset();
    if (chunk_cache_ != nullptr) {
        auto status = chunk_cache_->clear();
        if (!status) {
            first_failure = status;
        }
        chunk_cache_.reset();
    }

    const auto release = [this, &first_failure](rhi::RenderResourceHandle& handle) {
        if (device_ == nullptr || !handle.is_valid()) {
            return;
        }
        auto status = device_->release_resource(handle);
        if (!status && first_failure) {
            first_failure = status;
        }
        handle = {};
    };
    release(terrain_pipeline_);
    release(terrain_vertex_shader_);
    release(terrain_fragment_shader_);
    device_.reset();
    return first_failure;
}

core::Status Renderer::synchronize_chunks(world::WorldState& world, const RenderCamera& camera) {
    if (chunk_system_ == nullptr) {
        return core::Status::failure("renderer.not_initialized",
                                     "renderer must be initialized before chunk synchronization");
    }
    cpu_timings_.reset();
    frame_started_at_ = std::chrono::steady_clock::now();
    frame_timing_active_ = true;
    auto status = core::Status::ok();
    {
        profiling::ScopedCpuTimingZone synchronization_zone(
            cpu_timings_, profiling::CpuTimingZone::chunk_synchronization);
        status = chunk_system_->synchronize(world, camera);
    }
    update_frontend_stats(world.chunks().chunk_count());
    return status;
}

core::Status Renderer::process_chunk_loads(std::span<const world::ChunkStreamLoadReport> loads) {
    if (chunk_system_ == nullptr) {
        return core::Status::failure("renderer.not_initialized",
                                     "renderer must be initialized before processing chunk loads");
    }
    return chunk_system_->process_chunk_loads(loads);
}

core::Status Renderer::process_chunk_evictions(std::span<const world::ChunkIdentity> evictions) {
    if (chunk_system_ == nullptr) {
        return core::Status::failure(
            "renderer.not_initialized",
            "renderer must be initialized before processing chunk evictions");
    }
    return chunk_system_->process_chunk_evictions(evictions);
}

core::Status Renderer::process_chunk_evictions(const world::ChunkStreamEvictionReport& eviction) {
    if (chunk_system_ == nullptr) {
        return core::Status::failure(
            "renderer.not_initialized",
            "renderer must be initialized before processing chunk evictions");
    }
    return chunk_system_->process_chunk_evictions(eviction);
}

core::Result<rhi::RenderFrameStats> Renderer::render(const RenderCamera& camera) {
    if (device_ == nullptr || chunk_system_ == nullptr || frame_builder_ == nullptr) {
        return core::Result<rhi::RenderFrameStats>::failure(
            "renderer.not_initialized", "renderer must be initialized before rendering");
    }
    if (!frame_timing_active_) {
        cpu_timings_.reset();
        frame_started_at_ = std::chrono::steady_clock::now();
        frame_timing_active_ = true;
    }
    ChunkDrawList draws;
    {
        profiling::ScopedCpuTimingZone extraction_zone(cpu_timings_,
                                                       profiling::CpuTimingZone::render_extraction);
        draws = chunk_system_->build_draw_list(camera, std::move(draw_command_scratch_));
    }
    RenderCommandLists command_lists;
    command_lists.world_draws = std::move(draws.draws);
    if (command_lists.world_draws.empty()) {
        draw_command_scratch_ = std::move(command_lists.world_draws);
    }
    auto frame = [&]() {
        profiling::ScopedCpuTimingZone command_zone(cpu_timings_,
                                                    profiling::CpuTimingZone::command_build);
        return frame_builder_->build(camera, std::move(command_lists));
    }();
    if (!frame) {
        return core::Result<rhi::RenderFrameStats>::failure(frame.error().code,
                                                            frame.error().message);
    }
    auto executed = device_->execute_frame(frame.value());
    if (!frame.value().pass_commands.empty()) {
        draw_command_scratch_ = std::move(frame.value().pass_commands.front().draws);
    }
    if (!executed) {
        frame_timing_active_ = false;
        return executed;
    }
    update_frontend_stats(stats_.loaded_chunks);
    update_backend_stats(executed.value());
    stats_.cpu_frame_ms = std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - frame_started_at_)
                              .count();
    frame_timing_active_ = false;
    return executed;
}

core::Status Renderer::resize(rhi::RenderExtent extent) {
    if (device_ == nullptr || frame_builder_ == nullptr) {
        return core::Status::failure("renderer.not_initialized",
                                     "renderer must be initialized before resizing");
    }
    auto status = device_->resize(extent);
    if (!status) {
        return status;
    }
    return frame_builder_->resize(extent);
}

bool Renderer::is_initialized() const noexcept {
    return device_ != nullptr && chunk_cache_ != nullptr && chunk_system_ != nullptr &&
           frame_builder_ != nullptr && terrain_pipeline_.is_valid();
}

const ChunkRenderStats& Renderer::chunk_stats() const noexcept {
    return chunk_system_ == nullptr ? empty_chunk_stats : chunk_system_->stats();
}

const RendererStats& Renderer::stats() const noexcept {
    return stats_;
}

rhi::IRenderDevice* Renderer::device() noexcept {
    return device_.get();
}

const rhi::IRenderDevice* Renderer::device() const noexcept {
    return device_.get();
}

void Renderer::update_frontend_stats(std::size_t loaded_chunk_count) noexcept {
    const auto saturating_u32 = [](std::size_t value) noexcept {
        return static_cast<std::uint32_t>(
            std::min(value, static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())));
    };
    const auto& chunks = chunk_stats();
    stats_.render_extraction_ms =
        cpu_timings_.milliseconds(profiling::CpuTimingZone::render_extraction);
    stats_.chunk_synchronization_ms =
        cpu_timings_.milliseconds(profiling::CpuTimingZone::chunk_synchronization);
    stats_.culling_ms = chunks.culling_ms;
    stats_.draw_list_ms = chunks.draw_list_ms;
    stats_.command_build_ms =
        chunks.draw_list_ms + cpu_timings_.milliseconds(profiling::CpuTimingZone::command_build);
    stats_.chunk_snapshot_ms = chunks.chunk_snapshot_ms;
    stats_.meshing_ms = chunks.meshing_ms;
    stats_.upload_preparation_ms = chunks.upload_preparation_ms;
    stats_.upload_ms = chunks.upload_ms;
    stats_.loaded_chunks = saturating_u32(loaded_chunk_count);
    stats_.mesh_pending_chunks = saturating_u32(chunks.pending_mesh_count);
    stats_.upload_pending_chunks = saturating_u32(chunks.pending_upload_count);
    stats_.resident_chunks = saturating_u32(chunks.cache.resident_chunk_count);
    stats_.visible_chunks = saturating_u32(chunks.visible_chunk_count);
    stats_.culled_chunks = saturating_u32(chunks.culled_chunk_count);
    stats_.drawn_chunks = saturating_u32(chunks.draw_count);
    stats_.vertices = chunks.visible_vertex_count;
    stats_.triangles = chunks.visible_index_count / 3;
    stats_.resident_mesh_bytes = chunks.cache.resident_bytes;
    stats_.gpu_arena_capacity_bytes =
        chunks.cache.vertex_arena.capacity_bytes + chunks.cache.index_arena.capacity_bytes;
    stats_.gpu_arena_used_bytes =
        chunks.cache.vertex_arena.used_bytes + chunks.cache.index_arena.used_bytes;
    stats_.gpu_arena_free_bytes =
        chunks.cache.vertex_arena.free_bytes + chunks.cache.index_arena.free_bytes;
    const auto arena_free = stats_.gpu_arena_free_bytes;
    const auto arena_largest = chunks.cache.vertex_arena.largest_free_range_bytes +
                               chunks.cache.index_arena.largest_free_range_bytes;
    stats_.gpu_arena_fragmentation = arena_free == 0 ? 0.0
                                                     : 1.0 - static_cast<double>(arena_largest) /
                                                                 static_cast<double>(arena_free);
    stats_.pending_upload_bytes = chunks.pending_upload_bytes;
    stats_.uploaded_bytes_this_frame = chunks.uploaded_bytes;
}

void Renderer::update_backend_stats(const rhi::RenderFrameStats& frame) noexcept {
    stats_.frame_index = frame.frame_index;
    stats_.submission_serial = frame.submission_serial;
    stats_.completed_submission_serial = frame.completed_submission_serial;
    stats_.draw_calls = static_cast<std::uint32_t>(std::min(
        frame.draw_count, static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())));
    stats_.command_recording_ms = frame.cpu_command_recording_ms;
    stats_.gpu_wait_ms = frame.cpu_gpu_wait_ms;
    stats_.gpu_timing_valid = frame.gpu_timing_valid;
    stats_.gpu_timing_frame_index = frame.gpu_timing_frame_index;
    stats_.gpu_timing_latency_frames = frame.gpu_timing_latency_frames;
    stats_.gpu_upload_timing_valid = frame.gpu_upload_timing_valid;
    stats_.gpu_upload_submission_serial = frame.gpu_upload_submission_serial;
    stats_.gpu_frame_ms = frame.gpu_frame_ms;
    stats_.gpu_opaque_terrain_ms = frame.gpu_opaque_terrain_ms;
    stats_.gpu_upload_ms = frame.gpu_upload_ms;
    stats_.gpu_transfer_ms = frame.gpu_transfer_ms;
    stats_.gpu_final_copy_ms = frame.gpu_final_copy_ms;
}

core::Status Renderer::create_terrain_pipeline(std::span<const std::uint32_t> vertex_spirv,
                                               std::span<const std::uint32_t> fragment_spirv) {
    const auto material = core::PrototypeId::parse("base:materials/milestone_terrain");
    if (!material) {
        return core::Status::failure("renderer.invalid_terrain_material",
                                     "internal terrain material prototype id is invalid");
    }

    rhi::RenderPipelineLayoutDesc layout;
    layout.material_id = material.value();
    layout.shader_template = {"base", "shaders/terrain.vert"};
    layout.push_constant_ranges.push_back(
        {rhi::RenderShaderStageFlags::vertex, 0, sizeof(rhi::ChunkPushConstants)});
    layout.debug_name = "terrain_layout";
    auto layout_result = device_->bind_pipeline_layout(std::move(layout));
    if (!layout_result) {
        return core::Status::failure(layout_result.error().code, layout_result.error().message);
    }

    auto vertex_shader = device_->create_shader_module(
        {rhi::RenderShaderStage::vertex, "terrain_vertex"}, vertex_spirv);
    if (!vertex_shader) {
        return core::Status::failure(vertex_shader.error().code, vertex_shader.error().message);
    }
    terrain_vertex_shader_ = vertex_shader.value().handle;

    auto fragment_shader = device_->create_shader_module(
        {rhi::RenderShaderStage::fragment, "terrain_fragment"}, fragment_spirv);
    if (!fragment_shader) {
        return core::Status::failure(fragment_shader.error().code, fragment_shader.error().message);
    }
    terrain_fragment_shader_ = fragment_shader.value().handle;

    rhi::RenderGraphicsPipelineDesc pipeline;
    pipeline.vertex_shader = terrain_vertex_shader_;
    pipeline.fragment_shader = terrain_fragment_shader_;
    pipeline.material_id = material.value();
    pipeline.debug_name = "terrain_pipeline";
    pipeline.vertex_stride = sizeof(terrain::GpuChunkVertex);
    pipeline.vertex_attributes.assign(terrain::gpu_chunk_vertex_attributes.begin(),
                                      terrain::gpu_chunk_vertex_attributes.end());
    pipeline.topology = rhi::RenderPrimitiveTopology::triangle_list;
    pipeline.polygon_mode = rhi::RenderPolygonMode::fill;
    pipeline.cull_mode = rhi::RenderCullMode::back;
    pipeline.front_face = rhi::RenderFrontFace::counter_clockwise;
    pipeline.depth_test_enable = true;
    pipeline.depth_write_enable = true;
    pipeline.depth_compare = rhi::RenderCompareOperation::less;
    pipeline.blend_mode = rhi::RenderBlendMode::disabled;
    pipeline.color_target_format = rhi::RenderImageFormat::rgba8_unorm;
    pipeline.depth_target_format = rhi::RenderImageFormat::d32_sfloat;
    auto pipeline_result = device_->create_graphics_pipeline(std::move(pipeline));
    if (!pipeline_result) {
        return core::Status::failure(pipeline_result.error().code, pipeline_result.error().message);
    }
    terrain_pipeline_ = pipeline_result.value().handle;
    return core::Status::ok();
}

} // namespace heartstead::renderer
