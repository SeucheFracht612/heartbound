#include "engine/renderer/renderer.hpp"

#include "engine/renderer/terrain/gpu_chunk_vertex.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <limits>
#include <utility>

namespace heartstead::renderer {

namespace {

const ChunkRenderStats empty_chunk_stats{};

[[nodiscard]] std::vector<std::byte> make_terrain_tile(std::array<std::uint8_t, 3> color,
                                                       bool error = false) {
    constexpr std::uint32_t tile_size = 16;
    std::vector<std::byte> pixels(tile_size * tile_size * 4U);
    for (std::uint32_t y = 0; y < tile_size; ++y) {
        for (std::uint32_t x = 0; x < tile_size; ++x) {
            const auto offset = static_cast<std::size_t>(y * tile_size + x) * 4U;
            const bool alternate = ((x / 4U) + (y / 4U)) % 2U != 0;
            if (error) {
                pixels[offset] = static_cast<std::byte>(alternate ? 20U : 255U);
                pixels[offset + 1] = static_cast<std::byte>(0U);
                pixels[offset + 2] = static_cast<std::byte>(alternate ? 20U : 255U);
            } else {
                const auto scale = alternate ? 0.82F : 1.0F;
                for (std::size_t channel = 0; channel < color.size(); ++channel) {
                    pixels[offset + channel] = static_cast<std::byte>(
                        static_cast<std::uint8_t>(static_cast<float>(color[channel]) * scale));
                }
            }
            pixels[offset + 3] = static_cast<std::byte>(255U);
        }
    }
    return pixels;
}

[[nodiscard]] ShaderProgramDesc
make_terrain_shader_program(std::span<const std::uint32_t> vertex_spirv,
                            std::span<const std::uint32_t> fragment_spirv) {
    ShaderProgramDesc shader_program;
    shader_program.id = "terrain";
    shader_program.stages = {
        {rhi::RenderShaderStage::vertex,
         "main",
         {vertex_spirv.begin(), vertex_spirv.end()},
         "terrain.vert.spv"},
        {rhi::RenderShaderStage::fragment,
         "main",
         {fragment_spirv.begin(), fragment_spirv.end()},
         "terrain.frag.spv"},
    };
    shader_program.interface.vertex_stride = sizeof(terrain::GpuChunkVertex);
    for (const auto& attribute : terrain::gpu_chunk_vertex_attributes) {
        shader_program.interface.vertex_inputs.push_back({attribute.location, attribute.format});
    }
    shader_program.interface.descriptors = {
        {"terrain_textures", rhi::RenderDescriptorKind::sampled_texture, 0, true},
        {"voxel_materials", rhi::RenderDescriptorKind::storage_buffer, 1, true},
    };
    shader_program.interface.push_constant_ranges.push_back(
        {rhi::RenderShaderStageFlags::vertex, 0, sizeof(rhi::ChunkPushConstants)});
    shader_program.dependencies = {"gpu_chunk_vertex_v1", "gpu_voxel_material_v1",
                                   "chunk_push_constants_v1"};
    return shader_program;
}

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
    shader_manager_ = std::make_unique<ShaderManager>(*device_, desc.development_shader_hot_reload);
    sampler_cache_ = std::make_unique<SamplerCache>(*device_);
    texture_manager_ = std::make_unique<TextureManager>(*device_);
    material_cache_ = std::make_unique<MaterialRuntimeCache>(*device_);
    pipeline_cache_ = std::make_unique<PipelineCache>(*device_, *shader_manager_);
    auto fallback_status = texture_manager_->initialize_fallbacks();
    if (!fallback_status) {
        const auto error = fallback_status.error();
        (void)shutdown();
        return core::Status::failure(error.code, error.message);
    }
    auto pipeline_status = create_terrain_pipeline(desc.terrain_vertex_spirv,
                                                   desc.terrain_fragment_spirv, desc.voxel_palette);
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

    const auto remember_failure = [&first_failure](core::Status status) {
        if (!status && first_failure) {
            first_failure = status;
        }
    };
    if (pipeline_cache_ != nullptr) {
        remember_failure(pipeline_cache_->shutdown());
        pipeline_cache_.reset();
    }
    if (material_cache_ != nullptr) {
        remember_failure(material_cache_->shutdown());
        material_cache_.reset();
    }
    if (texture_manager_ != nullptr) {
        remember_failure(texture_manager_->shutdown());
        texture_manager_.reset();
    }
    if (sampler_cache_ != nullptr) {
        remember_failure(sampler_cache_->shutdown());
        sampler_cache_.reset();
    }
    if (shader_manager_ != nullptr) {
        remember_failure(shader_manager_->shutdown());
        shader_manager_.reset();
    }
    terrain_pipeline_ = {};
    terrain_pipeline_key_ = {};
    terrain_shader_program_ = {};
    terrain_texture_array_ = {};
    terrain_sampler_ = {};
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

core::Status Renderer::reload_terrain_shaders(std::span<const std::uint32_t> vertex_spirv,
                                              std::span<const std::uint32_t> fragment_spirv) {
    if (!is_initialized() || shader_manager_ == nullptr || pipeline_cache_ == nullptr ||
        chunk_system_ == nullptr) {
        return core::Status::failure("renderer.not_initialized",
                                     "renderer must be initialized before shader reload");
    }
    auto status = shader_manager_->reload_program(
        terrain_shader_program_, make_terrain_shader_program(vertex_spirv, fragment_spirv));
    if (!status) {
        return status;
    }
    status = pipeline_cache_->rebuild_program(terrain_shader_program_);
    if (!status) {
        return status;
    }
    auto rebuilt = pipeline_cache_->find(terrain_pipeline_key_);
    if (!rebuilt) {
        return core::Status::failure(rebuilt.error().code, rebuilt.error().message);
    }
    terrain_pipeline_ = rebuilt.value();
    return chunk_system_->set_terrain_pipeline(terrain_pipeline_);
}

bool Renderer::is_initialized() const noexcept {
    return device_ != nullptr && chunk_cache_ != nullptr && chunk_system_ != nullptr &&
           frame_builder_ != nullptr && shader_manager_ != nullptr && texture_manager_ != nullptr &&
           material_cache_ != nullptr && pipeline_cache_ != nullptr && terrain_pipeline_.is_valid();
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
    stats_.drawn_chunks = saturating_u32(chunks.drawn_chunk_count);
    stats_.residency_suppressed_chunks = saturating_u32(chunks.residency_suppressed_chunk_count);
    if (texture_manager_ != nullptr) {
        stats_.resident_textures = saturating_u32(texture_manager_->stats().resident_texture_count);
        stats_.resident_texture_bytes = texture_manager_->stats().resident_texture_bytes;
    }
    if (material_cache_ != nullptr) {
        stats_.runtime_materials = saturating_u32(material_cache_->stats().resident_material_count);
    }
    if (pipeline_cache_ != nullptr) {
        stats_.resident_pipelines =
            saturating_u32(pipeline_cache_->stats().resident_pipeline_count);
    }
    stats_.vertices = chunks.visible_vertex_count;
    stats_.triangles = chunks.visible_index_count / 3;
    stats_.resident_mesh_bytes = chunks.cache.resident_bytes;
    stats_.gpu_terrain_budget_bytes = chunks.gpu_terrain_budget_bytes;
    stats_.distance_evicted_meshes = chunks.distance_evicted_mesh_count;
    stats_.memory_pressure_evicted_meshes = chunks.memory_pressure_evicted_mesh_count;
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
    stats_.pipeline_switches = static_cast<std::uint32_t>(
        std::min(frame.pipeline_bind_count,
                 static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())));
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
                                               std::span<const std::uint32_t> fragment_spirv,
                                               const world::VoxelPalette* voxel_palette) {
    if (shader_manager_ == nullptr || sampler_cache_ == nullptr || texture_manager_ == nullptr ||
        material_cache_ == nullptr || pipeline_cache_ == nullptr) {
        return core::Status::failure("renderer.runtime_assets_uninitialized",
                                     "terrain runtime asset managers must be initialized first");
    }
    const auto material = core::PrototypeId::parse("base:materials/milestone_terrain");
    if (!material) {
        return core::Status::failure("renderer.invalid_terrain_material",
                                     "internal terrain material prototype id is invalid");
    }

    TerrainTextureArrayBuilder texture_builder(16, 16);
    auto layer = texture_builder.add_layer("error", make_terrain_tile({255, 0, 255}, true));
    if (!layer) {
        return core::Status::failure(layer.error().code, layer.error().message);
    }
    constexpr std::array<std::array<std::uint8_t, 3>, 6> terrain_colors{
        std::array<std::uint8_t, 3>{74, 145, 57},   std::array<std::uint8_t, 3>{118, 78, 46},
        std::array<std::uint8_t, 3>{112, 116, 124}, std::array<std::uint8_t, 3>{184, 162, 98},
        std::array<std::uint8_t, 3>{54, 111, 48},   std::array<std::uint8_t, 3>{127, 91, 59},
    };
    for (std::size_t index = 0; index < terrain_colors.size(); ++index) {
        layer = texture_builder.add_layer("terrain_" + std::to_string(index + 1),
                                          make_terrain_tile(terrain_colors[index]));
        if (!layer) {
            return core::Status::failure(layer.error().code, layer.error().message);
        }
    }
    auto texture_desc = texture_builder.build("terrain_texture_array");
    if (!texture_desc) {
        return core::Status::failure(texture_desc.error().code, texture_desc.error().message);
    }
    auto texture = texture_manager_->create_texture(std::move(texture_desc).value());
    if (!texture) {
        return core::Status::failure(texture.error().code, texture.error().message);
    }
    terrain_texture_array_ = texture.value();
    const auto* texture_view = texture_manager_->find(terrain_texture_array_);
    if (texture_view == nullptr) {
        return core::Status::failure("renderer.terrain_texture_missing",
                                     "terrain texture array disappeared after creation");
    }

    rhi::RenderSamplerDesc sampler_desc;
    sampler_desc.min_filter = rhi::RenderSamplerFilter::nearest;
    sampler_desc.mag_filter = rhi::RenderSamplerFilter::nearest;
    sampler_desc.mipmap_mode = rhi::RenderSamplerMipmapMode::linear;
    sampler_desc.max_lod = static_cast<float>(texture_view->mip_levels - 1U);
    sampler_desc.debug_name = "terrain_sampler";
    auto sampler = sampler_cache_->get(std::move(sampler_desc));
    if (!sampler) {
        return core::Status::failure(sampler.error().code, sampler.error().message);
    }
    terrain_sampler_ = sampler.value();

    std::vector<std::uint16_t> voxel_types;
    if (voxel_palette != nullptr && !voxel_palette->empty()) {
        const auto definitions = voxel_palette->definitions();
        voxel_types.reserve(definitions.size());
        for (const auto* definition : definitions) {
            voxel_types.push_back(definition->type);
        }
    } else {
        voxel_types.reserve(255);
        for (std::uint16_t type = 1; type <= 255; ++type) {
            voxel_types.push_back(type);
        }
    }
    for (const auto type : voxel_types) {
        auto runtime_id =
            core::PrototypeId::parse("base:materials/runtime_voxel_" + std::to_string(type));
        if (!runtime_id) {
            return core::Status::failure("renderer.invalid_runtime_material_id",
                                         "generated voxel material id is invalid");
        }
        MaterialRuntimeDesc runtime_material;
        runtime_material.id = runtime_id.value();
        runtime_material.voxel_type = type;
        runtime_material.side_texture = 1U + (static_cast<std::uint32_t>(type) - 1U) % 6U;
        runtime_material.top_texture = runtime_material.side_texture;
        runtime_material.bottom_texture = runtime_material.side_texture;
        auto inserted = material_cache_->upsert(std::move(runtime_material));
        if (!inserted) {
            return core::Status::failure(inserted.error().code, inserted.error().message);
        }
    }
    auto material_status = material_cache_->synchronize_gpu();
    if (!material_status) {
        return material_status;
    }

    auto shader =
        shader_manager_->create_program(make_terrain_shader_program(vertex_spirv, fragment_spirv));
    if (!shader) {
        return core::Status::failure(shader.error().code, shader.error().message);
    }
    terrain_shader_program_ = shader.value();

    rhi::RenderPipelineLayoutDesc layout;
    layout.material_id = material.value();
    layout.shader_template = {"base", "shaders/terrain.vert"};
    layout.descriptors = {
        {"terrain_textures", rhi::RenderDescriptorKind::sampled_texture, 0, true},
        {"voxel_materials", rhi::RenderDescriptorKind::storage_buffer, 1, true},
    };
    layout.push_constant_ranges.push_back(
        {rhi::RenderShaderStageFlags::vertex, 0, sizeof(rhi::ChunkPushConstants)});
    layout.debug_name = "terrain_layout";

    rhi::RenderGraphicsPipelineDesc pipeline;
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
    terrain_pipeline_key_ = {};
    terrain_pipeline_key_.shader_program = terrain_shader_program_;
    terrain_pipeline_key_.vertex_layout =
        hash_vertex_layout(pipeline.vertex_stride, pipeline.vertex_attributes);
    auto pipeline_result = pipeline_cache_->prewarm(terrain_pipeline_key_, layout, pipeline);
    if (!pipeline_result) {
        return core::Status::failure(pipeline_result.error().code, pipeline_result.error().message);
    }
    terrain_pipeline_ = pipeline_result.value();

    const rhi::RenderDescriptorWrite texture_write{
        material.value(), "terrain_textures", texture_view->image, 0, 0, terrain_sampler_};
    auto texture_binding =
        device_->write_descriptors(std::span<const rhi::RenderDescriptorWrite>{&texture_write, 1});
    if (!texture_binding) {
        return core::Status::failure(texture_binding.error().code, texture_binding.error().message);
    }
    material_status =
        material_cache_->write_gpu_table_descriptor(material.value(), "voxel_materials");
    if (!material_status) {
        return material_status;
    }
    pipeline_cache_->seal();
    return core::Status::ok();
}

} // namespace heartstead::renderer
