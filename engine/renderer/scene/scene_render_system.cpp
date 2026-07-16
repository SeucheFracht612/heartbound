#include "engine/renderer/scene/scene_render_system.hpp"

#include <algorithm>
#include <limits>
#include <ranges>
#include <utility>

namespace heartstead::renderer {

namespace {

[[nodiscard]] float bounds_depth_squared(const math::Bounds3f& bounds) noexcept {
    const auto center = (bounds.min + bounds.max) * 0.5F;
    return math::length_squared(center);
}

} // namespace

bool ScenePipelineSet::is_valid() const noexcept {
    return opaque.is_valid() && alpha_tested.is_valid() && transparent.is_valid();
}

rhi::RenderResourceHandle ScenePipelineSet::for_layer(RenderLayer layer) const noexcept {
    switch (layer) {
    case RenderLayer::opaque:
        return opaque;
    case RenderLayer::alpha_tested:
        return alpha_tested;
    case RenderLayer::transparent:
        return transparent;
    }
    return {};
}

core::Status SceneRenderConfig::validate() const {
    constexpr auto maximum_size = std::numeric_limits<std::size_t>::max();
    if (maximum_instances_per_frame == 0 || buffered_frames < 2 || buffered_frames > 8 ||
        maximum_instances_per_frame > maximum_size / sizeof(GpuObjectInstance) / buffered_frames) {
        return core::Status::failure(
            "scene_render.invalid_config",
            "scene instance capacity must be nonzero and use two to eight buffered frames");
    }
    return core::Status::ok();
}

SceneRenderSystem::SceneRenderSystem(rhi::IRenderDevice& device, MeshManager& meshes,
                                     ScenePipelineSet pipelines,
                                     core::PrototypeId pipeline_material)
    : device_(&device), meshes_(&meshes), pipelines_(pipelines),
      pipeline_material_(std::move(pipeline_material)) {}

SceneRenderSystem::~SceneRenderSystem() {
    (void)shutdown();
}

core::Status SceneRenderSystem::initialize(SceneRenderConfig config) {
    if (instance_buffer_.is_valid()) {
        return core::Status::failure("scene_render.already_initialized",
                                     "scene render system cannot be initialized twice");
    }
    auto status = config.validate();
    if (!status) {
        return status;
    }
    if (!pipelines_.is_valid() || !pipeline_material_.is_valid()) {
        return core::Status::failure("scene_render.invalid_pipeline",
                                     "scene render system requires retained pipelines");
    }
    config_ = config;
    const auto instance_count = static_cast<std::size_t>(config_.maximum_instances_per_frame) *
                                config_.buffered_frames;
    const auto byte_size = instance_count * sizeof(GpuObjectInstance);
    auto buffer = device_->create_buffer({rhi::RenderBufferUsage::storage, byte_size,
                                          "scene_instance_buffer",
                                          rhi::RenderBufferMemory::device_local});
    if (!buffer) {
        return core::Status::failure(buffer.error().code, buffer.error().message);
    }
    instance_buffer_ = buffer.value().handle;
    const rhi::RenderDescriptorWrite write{pipeline_material_, "object_instances",
                                           instance_buffer_, 0, byte_size};
    auto descriptor =
        device_->write_descriptors(std::span<const rhi::RenderDescriptorWrite>{&write, 1});
    if (!descriptor) {
        const auto error = descriptor.error();
        (void)device_->release_resource(instance_buffer_);
        instance_buffer_ = {};
        return core::Status::failure(error.code, error.message);
    }
    instance_scratch_.reserve(config_.maximum_instances_per_frame);
    stats_.instance_buffer_bytes = byte_size;
    return core::Status::ok();
}

core::Result<SceneDrawCommands>
SceneRenderSystem::build_draw_commands(const RenderScene& scene, const RenderCamera& camera,
                                       float simulation_alpha, SceneDrawCommands scratch) {
    if (!instance_buffer_.is_valid()) {
        return core::Result<SceneDrawCommands>::failure(
            "scene_render.not_initialized", "scene render system must be initialized first");
    }
    auto extracted = scene.extract(camera, simulation_alpha);
    if (!extracted) {
        return core::Result<SceneDrawCommands>::failure(extracted.error().code,
                                                        extracted.error().message);
    }
    scratch.opaque_and_cutout.clear();
    scratch.transparent.clear();
    instance_scratch_.clear();
    stats_ = {};
    stats_.scene = extracted.value().stats;
    stats_.instance_buffer_bytes =
        static_cast<std::uint64_t>(config_.maximum_instances_per_frame) *
        config_.buffered_frames * sizeof(GpuObjectInstance);

    const auto frame_slot = frame_number_ % config_.buffered_frames;
    const auto segment_instance_offset =
        static_cast<std::uint64_t>(frame_slot) * config_.maximum_instances_per_frame;
    const auto segment_byte_offset = segment_instance_offset * sizeof(GpuObjectInstance);

    for (auto& batch : extracted.value().batches) {
        if (instance_scratch_.size() >= config_.maximum_instances_per_frame) {
            stats_.dropped_instances += static_cast<std::uint32_t>(batch.instances.size());
            continue;
        }
        if (batch.layer == RenderLayer::transparent) {
            std::ranges::stable_sort(batch.instances, [](const RenderObjectInstance& left,
                                                         const RenderObjectInstance& right) {
                return bounds_depth_squared(left.camera_relative_bounds) >
                       bounds_depth_squared(right.camera_relative_bounds);
            });
        }
        const auto remaining = static_cast<std::size_t>(config_.maximum_instances_per_frame) -
                               instance_scratch_.size();
        const auto accepted = std::min(remaining, batch.instances.size());
        stats_.dropped_instances +=
            static_cast<std::uint32_t>(batch.instances.size() - accepted);
        if (accepted == 0) {
            continue;
        }
        const auto first_instance_in_segment = instance_scratch_.size();
        for (std::size_t index = 0; index < accepted; ++index) {
            const auto& instance = batch.instances[index];
            GpuObjectInstance gpu;
            gpu.camera_relative_transform = instance.camera_relative_transform;
            std::ranges::copy(instance.color, gpu.color);
            gpu.metadata[0] = static_cast<std::uint32_t>(instance.layer);
            instance_scratch_.push_back(gpu);
        }
        const auto* mesh = meshes_->find(batch.mesh);
        if (mesh == nullptr) {
            stats_.dropped_instances += static_cast<std::uint32_t>(accepted);
            instance_scratch_.resize(first_instance_in_segment);
            continue;
        }
        const auto vertex_offset = mesh->vertices.offset / sizeof(GpuStaticMeshVertex);
        const auto first_index = mesh->indices.offset / rhi::render_index_type_size(mesh->index_type);
        const auto first_instance = segment_instance_offset + first_instance_in_segment;
        if (vertex_offset > static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max()) ||
            first_index > std::numeric_limits<std::uint32_t>::max() ||
            first_instance > std::numeric_limits<std::uint32_t>::max()) {
            return core::Result<SceneDrawCommands>::failure(
                "scene_render.draw_offset_overflow", "scene mesh or instance offset exceeds RHI range");
        }
        rhi::RenderDrawCommand draw;
        draw.pipeline = pipelines_.for_layer(batch.layer);
        draw.vertex_buffer = mesh->vertices.buffer;
        draw.index_buffer = mesh->indices.buffer;
        draw.index_count = mesh->index_count;
        draw.first_index = static_cast<std::uint32_t>(first_index);
        draw.vertex_offset = static_cast<std::int32_t>(vertex_offset);
        draw.instance_count = static_cast<std::uint32_t>(accepted);
        draw.first_instance = static_cast<std::uint32_t>(first_instance);
        draw.index_type = mesh->index_type;
        if (batch.layer == RenderLayer::transparent) {
            draw.sort_depth = bounds_depth_squared(
                batch.instances.front().camera_relative_bounds);
            scratch.transparent.push_back(draw);
        } else {
            scratch.opaque_and_cutout.push_back(draw);
        }
        stats_.submitted_instances += static_cast<std::uint32_t>(accepted);
        ++stats_.draw_calls;
    }
    std::ranges::stable_sort(scratch.transparent, [](const auto& left, const auto& right) {
        return left.sort_depth > right.sort_depth;
    });
    if (!instance_scratch_.empty()) {
        const auto bytes = std::as_bytes(std::span<const GpuObjectInstance>{instance_scratch_});
        const rhi::RenderBufferWrite write{instance_buffer_,
                                           static_cast<std::size_t>(segment_byte_offset), bytes};
        auto upload =
            device_->upload_buffer_batch(std::span<const rhi::RenderBufferWrite>{&write, 1});
        if (!upload) {
            return core::Result<SceneDrawCommands>::failure(upload.error().code,
                                                            upload.error().message);
        }
        stats_.uploaded_instance_bytes = bytes.size();
    }
    ++frame_number_;
    scratch.stats = stats_;
    return core::Result<SceneDrawCommands>::success(std::move(scratch));
}

core::Status SceneRenderSystem::set_pipelines(ScenePipelineSet pipelines) noexcept {
    if (!pipelines.is_valid()) {
        return core::Status::failure("scene_render.invalid_pipeline",
                                     "scene pipelines must all be valid");
    }
    pipelines_ = pipelines;
    return core::Status::ok();
}

core::Status SceneRenderSystem::shutdown() {
    instance_scratch_.clear();
    stats_ = {};
    frame_number_ = 0;
    if (!instance_buffer_.is_valid()) {
        return core::Status::ok();
    }
    auto status = device_->release_resource(instance_buffer_);
    instance_buffer_ = {};
    return status;
}

const SceneRenderStats& SceneRenderSystem::stats() const noexcept {
    return stats_;
}

rhi::RenderResourceHandle SceneRenderSystem::instance_buffer() const noexcept {
    return instance_buffer_;
}

} // namespace heartstead::renderer
