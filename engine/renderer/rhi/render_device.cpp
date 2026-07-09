#include "engine/renderer/rhi/render_device.hpp"

#include "engine/renderer/rhi/render_frame_plan.hpp"
#include "engine/renderer/vulkan/vulkan_backend.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <ranges>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace heartstead::renderer::rhi {

namespace {

constexpr std::uint32_t spirv_magic = 0x07230203;

[[nodiscard]] bool is_valid_render_binding_name(std::string_view name) noexcept {
    if (name.empty() || name.front() == '.' || name.back() == '.') {
        return false;
    }
    for (const auto character : name) {
        const auto valid = (character >= 'a' && character <= 'z') ||
                           (character >= '0' && character <= '9') || character == '_' ||
                           character == '-' || character == '.';
        if (!valid) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool is_valid_virtual_path(const assets::VirtualPath& path) noexcept {
    return core::is_valid_namespace_id(path.namespace_id) &&
           core::is_valid_local_id(path.relative_path.generic_string());
}

[[nodiscard]] const RenderDescriptorBinding*
find_descriptor_binding(const RenderPipelineLayoutDesc& layout, std::string_view binding_name) {
    const auto found = std::ranges::find_if(layout.descriptors,
                                            [binding_name](const RenderDescriptorBinding& binding) {
                                                return binding.name == binding_name;
                                            });
    return found == layout.descriptors.end() ? nullptr : &*found;
}

class HeadlessRenderDevice final : public IRenderDevice {
  public:
    explicit HeadlessRenderDevice(RenderDeviceDesc desc) : desc_(std::move(desc)) {}

    [[nodiscard]] RenderBackend backend() const noexcept override {
        return RenderBackend::headless;
    }

    [[nodiscard]] std::string_view backend_name() const noexcept override {
        return render_backend_name(RenderBackend::headless);
    }

    [[nodiscard]] RenderDeviceCapabilities capabilities() const noexcept override {
        RenderDeviceCapabilities result;
        result.backend = RenderBackend::headless;
        result.max_extent = RenderExtent{16384, 16384};
        result.supports_present = true;
        result.supports_validation = desc_.enable_validation;
        result.supports_debug_markers = true;
        result.supports_shader_modules = true;
        result.supports_pipeline_layout = true;
        result.supports_compute_pipelines = true;
        result.supports_graphics_pipelines = true;
        result.supports_descriptor_writes = true;
        result.supports_buffer_upload = true;
        result.supports_image_upload = true;
        result.supports_draw_binding = true;
        result.headless = true;
        return result;
    }

    [[nodiscard]] RenderExtent current_extent() const noexcept override {
        return desc_.initial_extent;
    }

    [[nodiscard]] std::uint64_t completed_frame_count() const noexcept override {
        return completed_frame_count_;
    }

    [[nodiscard]] std::size_t live_resource_count() const noexcept override {
        return resources_.size() + image_resources_.size() + shader_modules_.size() +
               compute_pipelines_.size() + graphics_pipelines_.size();
    }

    [[nodiscard]] core::Status resize(RenderExtent extent) override {
        auto status = validate_render_extent(extent);
        if (!status) {
            return status;
        }
        desc_.initial_extent = extent;
        return core::Status::ok();
    }

    [[nodiscard]] core::Result<RenderFrameStats> render_frame(RenderFrameDesc desc) override {
        const auto use_current_extent =
            desc.output_extent.width == 0 && desc.output_extent.height == 0;
        const auto extent = use_current_extent ? current_extent() : desc.output_extent;
        const auto plan = make_clear_present_frame_plan(extent, desc.clear_color, desc.present);
        return execute_frame_plan(plan);
    }

    [[nodiscard]] core::Result<RenderFrameStats>
    execute_frame_plan(const RenderFramePlan& plan) override {
        auto execution_plan = plan.build_execution_plan();
        if (!execution_plan) {
            return core::Result<RenderFrameStats>::failure(execution_plan.error().code,
                                                           execution_plan.error().message);
        }

        RenderFrameStats stats;
        stats.backend = backend();
        stats.frame_index = completed_frame_count_;
        stats.extent = execution_plan.value().extent;
        stats.clear_color = plan.first_clear_color();
        stats.presented = plan.has_present_pass();
        stats.render_pass_count = execution_plan.value().ordered_passes.size();
        stats.present_pass_count = execution_plan.value().present_pass_count;
        stats.resource_use_count = execution_plan.value().resource_uses.size();
        stats.dependency_count = execution_plan.value().dependencies.size();
        stats.transition_count = execution_plan.value().transitions.size();
        stats.synchronization_barrier_count = execution_plan.value().transitions.size();
        stats.submitted_synchronization_barrier_count = execution_plan.value().transitions.size();
        ++completed_frame_count_;
        return core::Result<RenderFrameStats>::success(stats);
    }

    [[nodiscard]] core::Result<RenderUploadStats>
    upload_buffer(RenderBufferDesc desc, std::span<const std::byte> bytes) override {
        auto status = validate_render_buffer_upload(desc, bytes);
        if (!status) {
            return core::Result<RenderUploadStats>::failure(status.error().code,
                                                            status.error().message);
        }

        const RenderResourceHandle handle{next_resource_id_++};
        resources_.emplace(handle.value, std::move(desc));

        RenderUploadStats stats;
        stats.backend = backend();
        stats.handle = handle;
        stats.usage = resources_.find(handle.value)->second.usage;
        stats.byte_size = bytes.size();
        stats.live_resource_count = resources_.size();
        stats.gpu_backed = false;
        return core::Result<RenderUploadStats>::success(stats);
    }

    [[nodiscard]] core::Result<RenderImageUploadStats>
    upload_image(RenderImageDesc desc, std::span<const std::byte> bytes) override {
        auto status = validate_render_image_upload(desc, bytes);
        if (!status) {
            return core::Result<RenderImageUploadStats>::failure(status.error().code,
                                                                 status.error().message);
        }

        const RenderResourceHandle handle{next_resource_id_++};
        image_resources_.emplace(handle.value, desc);

        RenderImageUploadStats stats;
        stats.backend = backend();
        stats.handle = handle;
        stats.format = desc.format;
        stats.width = desc.width;
        stats.height = desc.height;
        stats.byte_size = bytes.size();
        stats.live_resource_count = live_resource_count();
        stats.gpu_backed = false;
        return core::Result<RenderImageUploadStats>::success(stats);
    }

    [[nodiscard]] core::Result<RenderShaderModuleStats>
    create_shader_module(RenderShaderModuleDesc desc,
                         std::span<const std::uint32_t> spirv_words) override {
        auto status = validate_render_shader_module_upload(desc, spirv_words);
        if (!status) {
            return core::Result<RenderShaderModuleStats>::failure(status.error().code,
                                                                  status.error().message);
        }

        const RenderResourceHandle handle{next_resource_id_++};
        shader_modules_.emplace(handle.value, std::move(desc));

        RenderShaderModuleStats stats;
        stats.backend = backend();
        stats.handle = handle;
        stats.stage = shader_modules_.find(handle.value)->second.stage;
        stats.word_count = spirv_words.size();
        stats.live_shader_module_count = shader_modules_.size();
        stats.gpu_backed = false;
        return core::Result<RenderShaderModuleStats>::success(stats);
    }

    [[nodiscard]] core::Result<RenderPipelineLayoutStats>
    bind_pipeline_layout(RenderPipelineLayoutDesc desc) override {
        auto status = validate_render_pipeline_layout_shape(desc);
        if (!status) {
            return core::Result<RenderPipelineLayoutStats>::failure(status.error().code,
                                                                    status.error().message);
        }

        RenderPipelineLayoutStats stats;
        stats.backend = backend();
        stats.material_id = desc.material_id;
        stats.pipeline_version = desc.pipeline_version;
        stats.descriptor_count = desc.descriptors.size();
        stats.sampled_texture_count = static_cast<std::size_t>(
            std::ranges::count_if(desc.descriptors, [](const RenderDescriptorBinding& binding) {
                return binding.kind == RenderDescriptorKind::sampled_texture;
            }));
        stats.uniform_count = desc.descriptors.size() - stats.sampled_texture_count;
        stats.gpu_backed = false;

        destroy_compute_pipelines_for_material(desc.material_id);
        destroy_graphics_pipelines_for_material(desc.material_id);
        destroy_descriptor_writes_for_material(desc.material_id);
        pipeline_layouts_.insert_or_assign(desc.material_id.value(), std::move(desc));
        stats.bound_pipeline_count = pipeline_layouts_.size();
        return core::Result<RenderPipelineLayoutStats>::success(stats);
    }

    [[nodiscard]] core::Result<RenderComputePipelineStats>
    create_compute_pipeline(RenderComputePipelineDesc desc) override {
        auto status = validate_render_compute_pipeline_shape(desc);
        if (!status) {
            return core::Result<RenderComputePipelineStats>::failure(status.error().code,
                                                                     status.error().message);
        }
        const auto shader_module = shader_modules_.find(desc.compute_shader.value);
        if (shader_module == shader_modules_.end()) {
            return core::Result<RenderComputePipelineStats>::failure(
                "renderer.unknown_shader_module",
                "compute pipeline references a shader module handle not owned by this device");
        }
        if (shader_module->second.stage != RenderShaderStage::compute) {
            return core::Result<RenderComputePipelineStats>::failure(
                "renderer.invalid_compute_shader_stage",
                "compute pipeline shader module must have compute stage");
        }
        if (!pipeline_layouts_.contains(desc.material_id.value())) {
            return core::Result<RenderComputePipelineStats>::failure(
                "renderer.unbound_compute_pipeline_layout",
                "compute pipeline material must have a bound pipeline layout");
        }

        const RenderResourceHandle handle{next_resource_id_++};
        compute_pipelines_.emplace(handle.value, desc);

        RenderComputePipelineStats stats;
        stats.backend = backend();
        stats.handle = handle;
        stats.compute_shader = desc.compute_shader;
        stats.material_id = desc.material_id;
        stats.live_compute_pipeline_count = compute_pipelines_.size();
        stats.gpu_backed = false;
        return core::Result<RenderComputePipelineStats>::success(stats);
    }

    [[nodiscard]] core::Result<RenderGraphicsPipelineStats>
    create_graphics_pipeline(RenderGraphicsPipelineDesc desc) override {
        auto status = validate_render_graphics_pipeline_shape(desc);
        if (!status) {
            return core::Result<RenderGraphicsPipelineStats>::failure(status.error().code,
                                                                      status.error().message);
        }
        const auto vertex_shader = shader_modules_.find(desc.vertex_shader.value);
        if (vertex_shader == shader_modules_.end()) {
            return core::Result<RenderGraphicsPipelineStats>::failure(
                "renderer.unknown_vertex_shader",
                "graphics pipeline references a vertex shader handle not owned by this device");
        }
        if (vertex_shader->second.stage != RenderShaderStage::vertex) {
            return core::Result<RenderGraphicsPipelineStats>::failure(
                "renderer.invalid_vertex_shader_stage",
                "graphics pipeline vertex shader must have vertex stage");
        }
        const auto fragment_shader = shader_modules_.find(desc.fragment_shader.value);
        if (fragment_shader == shader_modules_.end()) {
            return core::Result<RenderGraphicsPipelineStats>::failure(
                "renderer.unknown_fragment_shader",
                "graphics pipeline references a fragment shader handle not owned by this device");
        }
        if (fragment_shader->second.stage != RenderShaderStage::fragment) {
            return core::Result<RenderGraphicsPipelineStats>::failure(
                "renderer.invalid_fragment_shader_stage",
                "graphics pipeline fragment shader must have fragment stage");
        }
        if (!pipeline_layouts_.contains(desc.material_id.value())) {
            return core::Result<RenderGraphicsPipelineStats>::failure(
                "renderer.unbound_graphics_pipeline_layout",
                "graphics pipeline material must have a bound pipeline layout");
        }

        const RenderResourceHandle handle{next_resource_id_++};
        graphics_pipelines_.emplace(handle.value, desc);

        RenderGraphicsPipelineStats stats;
        stats.backend = backend();
        stats.handle = handle;
        stats.vertex_shader = desc.vertex_shader;
        stats.fragment_shader = desc.fragment_shader;
        stats.material_id = desc.material_id;
        stats.live_graphics_pipeline_count = graphics_pipelines_.size();
        stats.gpu_backed = false;
        return core::Result<RenderGraphicsPipelineStats>::success(stats);
    }

    [[nodiscard]] core::Result<RenderDescriptorWriteStats>
    write_descriptors(std::span<const RenderDescriptorWrite> writes) override {
        auto status = validate_render_descriptor_writes_shape(writes);
        if (!status) {
            return core::Result<RenderDescriptorWriteStats>::failure(status.error().code,
                                                                     status.error().message);
        }

        std::unordered_set<std::string> materials;
        std::size_t uniform_write_count = 0;
        std::size_t sampled_texture_write_count = 0;
        for (const auto& write : writes) {
            const auto layout = pipeline_layouts_.find(write.material_id.value());
            if (layout == pipeline_layouts_.end()) {
                return core::Result<RenderDescriptorWriteStats>::failure(
                    "renderer.unbound_descriptor_layout",
                    "descriptor write material must have a bound pipeline layout");
            }
            const auto* binding = find_descriptor_binding(layout->second, write.binding_name);
            if (binding == nullptr) {
                return core::Result<RenderDescriptorWriteStats>::failure(
                    "renderer.unknown_descriptor_binding",
                    "descriptor write binding does not exist in the material pipeline layout");
            }
            if (binding->kind == RenderDescriptorKind::sampled_texture) {
                const auto image = image_resources_.find(write.resource.value);
                if (image == image_resources_.end()) {
                    if (resources_.contains(write.resource.value)) {
                        return core::Result<RenderDescriptorWriteStats>::failure(
                            "renderer.invalid_descriptor_resource_usage",
                            "sampled texture descriptor writes must reference an image resource");
                    }
                    return core::Result<RenderDescriptorWriteStats>::failure(
                        "renderer.unknown_descriptor_resource",
                        "descriptor write resource handle is not owned by this device");
                }
                if (write.byte_offset != 0 || write.byte_size != 0) {
                    return core::Result<RenderDescriptorWriteStats>::failure(
                        "renderer.invalid_sampled_texture_descriptor_range",
                        "sampled texture descriptor writes must not specify a byte range");
                }
                materials.insert(write.material_id.value());
                ++sampled_texture_write_count;
                continue;
            }

            const auto resource = resources_.find(write.resource.value);
            if (resource == resources_.end()) {
                if (image_resources_.contains(write.resource.value)) {
                    return core::Result<RenderDescriptorWriteStats>::failure(
                        "renderer.invalid_descriptor_resource_usage",
                        "uniform descriptor writes must reference a uniform buffer resource");
                }
                return core::Result<RenderDescriptorWriteStats>::failure(
                    "renderer.unknown_descriptor_resource",
                    "descriptor write resource handle is not owned by this device");
            }
            if (resource->second.usage != RenderBufferUsage::uniform) {
                return core::Result<RenderDescriptorWriteStats>::failure(
                    "renderer.invalid_descriptor_resource_usage",
                    "uniform descriptor writes must reference a uniform buffer resource");
            }
            if (write.byte_size == 0) {
                return core::Result<RenderDescriptorWriteStats>::failure(
                    "renderer.invalid_descriptor_write_size",
                    "uniform descriptor write byte size must be non-zero");
            }
            if (write.byte_offset > resource->second.byte_size ||
                write.byte_size > resource->second.byte_size - write.byte_offset) {
                return core::Result<RenderDescriptorWriteStats>::failure(
                    "renderer.descriptor_write_out_of_range",
                    "descriptor write byte range must fit inside the referenced resource");
            }

            materials.insert(write.material_id.value());
            ++uniform_write_count;
        }

        for (const auto& write : writes) {
            descriptor_writes_.insert_or_assign(
                write.material_id.value() + "|" + write.binding_name, write);
        }

        RenderDescriptorWriteStats stats;
        stats.backend = backend();
        stats.write_count = writes.size();
        stats.uniform_write_count = uniform_write_count;
        stats.sampled_texture_write_count = sampled_texture_write_count;
        stats.material_count = materials.size();
        stats.gpu_backed = false;
        return core::Result<RenderDescriptorWriteStats>::success(stats);
    }

    [[nodiscard]] core::Result<RenderDrawStats>
    bind_mesh_draws(std::span<const RenderMeshBinding> draws) override {
        auto status = validate_render_mesh_bindings_shape(draws);
        if (!status) {
            return core::Result<RenderDrawStats>::failure(status.error().code,
                                                          status.error().message);
        }

        std::unordered_set<std::string> materials;
        RenderDrawStats stats;
        stats.backend = backend();
        stats.draw_count = draws.size();
        stats.gpu_backed = false;

        for (const auto& draw : draws) {
            const auto has_graphics_pipeline =
                std::ranges::any_of(graphics_pipelines_, [&draw](const auto& pipeline) {
                    return pipeline.second.material_id == draw.material_id;
                });
            if (!has_graphics_pipeline) {
                return core::Result<RenderDrawStats>::failure(
                    "renderer.unbound_material_graphics_pipeline",
                    "mesh draw material must have a graphics pipeline");
            }
            const auto vertex = resources_.find(draw.vertex_buffer.value);
            if (vertex == resources_.end()) {
                return core::Result<RenderDrawStats>::failure(
                    "renderer.unknown_vertex_buffer",
                    "mesh draw references a vertex buffer handle not owned by this device");
            }
            if (vertex->second.usage != RenderBufferUsage::vertex) {
                return core::Result<RenderDrawStats>::failure(
                    "renderer.invalid_vertex_buffer_usage",
                    "mesh draw vertex buffer must reference a vertex buffer resource");
            }

            if (draw.index_buffer.is_valid()) {
                const auto index = resources_.find(draw.index_buffer.value);
                if (index == resources_.end()) {
                    return core::Result<RenderDrawStats>::failure(
                        "renderer.unknown_index_buffer",
                        "mesh draw references an index buffer handle not owned by this device");
                }
                if (index->second.usage != RenderBufferUsage::index) {
                    return core::Result<RenderDrawStats>::failure(
                        "renderer.invalid_index_buffer_usage",
                        "mesh draw index buffer must reference an index buffer resource");
                }
                ++stats.indexed_draw_count;
            }

            materials.insert(draw.material_id.value());
            stats.total_vertices += draw.vertex_count;
            stats.total_indices += draw.index_count;
        }

        stats.material_count = materials.size();
        return core::Result<RenderDrawStats>::success(stats);
    }

    [[nodiscard]] core::Status release_resource(RenderResourceHandle handle) override {
        if (!handle.is_valid()) {
            return core::Status::failure("renderer.invalid_resource_handle",
                                         "render resource handle must be valid");
        }
        if (resources_.erase(handle.value) != 0) {
            destroy_descriptor_writes_for_resource(handle);
            return core::Status::ok();
        }
        if (image_resources_.erase(handle.value) != 0) {
            destroy_descriptor_writes_for_resource(handle);
            return core::Status::ok();
        }
        if (shader_modules_.erase(handle.value) != 0) {
            destroy_compute_pipelines_for_shader(handle);
            destroy_graphics_pipelines_for_shader(handle);
            return core::Status::ok();
        }
        if (compute_pipelines_.erase(handle.value) != 0) {
            return core::Status::ok();
        }
        if (graphics_pipelines_.erase(handle.value) != 0) {
            return core::Status::ok();
        }
        return core::Status::failure("renderer.unknown_resource",
                                     "render resource handle is not owned by this device");
    }

  private:
    void destroy_compute_pipelines_for_shader(RenderResourceHandle shader) {
        for (auto pipeline = compute_pipelines_.begin(); pipeline != compute_pipelines_.end();) {
            if (pipeline->second.compute_shader.value == shader.value) {
                pipeline = compute_pipelines_.erase(pipeline);
            } else {
                ++pipeline;
            }
        }
    }

    void destroy_compute_pipelines_for_material(const core::PrototypeId& material_id) {
        for (auto pipeline = compute_pipelines_.begin(); pipeline != compute_pipelines_.end();) {
            if (pipeline->second.material_id == material_id) {
                pipeline = compute_pipelines_.erase(pipeline);
            } else {
                ++pipeline;
            }
        }
    }

    void destroy_graphics_pipelines_for_shader(RenderResourceHandle shader) {
        for (auto pipeline = graphics_pipelines_.begin(); pipeline != graphics_pipelines_.end();) {
            if (pipeline->second.vertex_shader.value == shader.value ||
                pipeline->second.fragment_shader.value == shader.value) {
                pipeline = graphics_pipelines_.erase(pipeline);
            } else {
                ++pipeline;
            }
        }
    }

    void destroy_graphics_pipelines_for_material(const core::PrototypeId& material_id) {
        for (auto pipeline = graphics_pipelines_.begin(); pipeline != graphics_pipelines_.end();) {
            if (pipeline->second.material_id == material_id) {
                pipeline = graphics_pipelines_.erase(pipeline);
            } else {
                ++pipeline;
            }
        }
    }

    void destroy_descriptor_writes_for_resource(RenderResourceHandle resource) {
        for (auto write = descriptor_writes_.begin(); write != descriptor_writes_.end();) {
            if (write->second.resource.value == resource.value) {
                write = descriptor_writes_.erase(write);
            } else {
                ++write;
            }
        }
    }

    void destroy_descriptor_writes_for_material(const core::PrototypeId& material_id) {
        for (auto write = descriptor_writes_.begin(); write != descriptor_writes_.end();) {
            if (write->second.material_id == material_id) {
                write = descriptor_writes_.erase(write);
            } else {
                ++write;
            }
        }
    }

    RenderDeviceDesc desc_;
    std::uint64_t completed_frame_count_ = 0;
    std::uint64_t next_resource_id_ = 1;
    std::unordered_map<std::uint64_t, RenderBufferDesc> resources_;
    std::unordered_map<std::uint64_t, RenderImageDesc> image_resources_;
    std::unordered_map<std::uint64_t, RenderShaderModuleDesc> shader_modules_;
    std::unordered_map<std::uint64_t, RenderComputePipelineDesc> compute_pipelines_;
    std::unordered_map<std::uint64_t, RenderGraphicsPipelineDesc> graphics_pipelines_;
    std::unordered_map<std::string, RenderDescriptorWrite> descriptor_writes_;
    std::unordered_map<std::string, RenderPipelineLayoutDesc> pipeline_layouts_;
};

} // namespace

bool RenderExtent::is_valid() const noexcept {
    return width > 0 && height > 0;
}

core::Result<std::unique_ptr<IRenderDevice>> create_render_device(RenderDeviceDesc desc) {
    auto status = validate_render_device_desc(desc);
    if (!status) {
        return core::Result<std::unique_ptr<IRenderDevice>>::failure(status.error().code,
                                                                     status.error().message);
    }

    switch (desc.backend) {
    case RenderBackend::headless:
        return core::Result<std::unique_ptr<IRenderDevice>>::success(
            std::make_unique<HeadlessRenderDevice>(std::move(desc)));
    case RenderBackend::vulkan:
        return heartstead::renderer::vulkan::create_device(std::move(desc));
    }

    return core::Result<std::unique_ptr<IRenderDevice>>::failure("renderer.unknown_backend",
                                                                 "unknown renderer backend");
}

core::Status validate_render_device_desc(const RenderDeviceDesc& desc) {
    if (desc.application_name.empty()) {
        return core::Status::failure("renderer.invalid_application_name",
                                     "render device application name must not be empty");
    }
    return validate_render_extent(desc.initial_extent);
}

core::Status validate_render_extent(RenderExtent extent) {
    if (!extent.is_valid()) {
        return core::Status::failure("renderer.invalid_extent",
                                     "render extent width and height must be non-zero");
    }
    return core::Status::ok();
}

core::Status validate_render_buffer_upload(const RenderBufferDesc& desc,
                                           std::span<const std::byte> bytes) {
    if (desc.byte_size == 0) {
        return core::Status::failure("renderer.invalid_buffer_size",
                                     "render buffer byte size must be non-zero");
    }
    if (bytes.empty()) {
        return core::Status::failure("renderer.empty_buffer_upload",
                                     "render buffer upload bytes must be non-empty");
    }
    if (desc.byte_size != bytes.size()) {
        return core::Status::failure("renderer.buffer_upload_size_mismatch",
                                     "render buffer upload byte size must match the descriptor");
    }
    switch (desc.usage) {
    case RenderBufferUsage::vertex:
    case RenderBufferUsage::index:
    case RenderBufferUsage::uniform:
    case RenderBufferUsage::storage:
        return core::Status::ok();
    }
    return core::Status::failure("renderer.unknown_buffer_usage", "unknown render buffer usage");
}

core::Status validate_render_image_upload(const RenderImageDesc& desc,
                                          std::span<const std::byte> bytes) {
    if (desc.width == 0 || desc.height == 0) {
        return core::Status::failure("renderer.invalid_image_extent",
                                     "render image width and height must be non-zero");
    }
    const auto bytes_per_pixel = render_image_format_bytes_per_pixel(desc.format);
    if (bytes_per_pixel == 0) {
        return core::Status::failure("renderer.unknown_image_format",
                                     "unknown render image format");
    }
    if (desc.width > std::numeric_limits<std::size_t>::max() / bytes_per_pixel ||
        desc.height > std::numeric_limits<std::size_t>::max() /
                          (static_cast<std::size_t>(desc.width) * bytes_per_pixel)) {
        return core::Status::failure("renderer.image_upload_too_large",
                                     "render image upload byte size would overflow");
    }
    const auto expected_size = static_cast<std::size_t>(desc.width) *
                               static_cast<std::size_t>(desc.height) * bytes_per_pixel;
    if (bytes.empty()) {
        return core::Status::failure("renderer.empty_image_upload",
                                     "render image upload bytes must be non-empty");
    }
    if (bytes.size() != expected_size) {
        return core::Status::failure("renderer.image_upload_size_mismatch",
                                     "render image upload byte size must match the image extent "
                                     "and format");
    }
    return core::Status::ok();
}

core::Status validate_render_shader_module_upload(const RenderShaderModuleDesc& desc,
                                                  std::span<const std::uint32_t> spirv_words) {
    switch (desc.stage) {
    case RenderShaderStage::vertex:
    case RenderShaderStage::fragment:
    case RenderShaderStage::compute:
        break;
    }
    if (spirv_words.size() < 5) {
        return core::Status::failure("renderer.empty_shader_module",
                                     "shader module SPIR-V must contain a header");
    }
    if (spirv_words.front() != spirv_magic) {
        return core::Status::failure("renderer.invalid_spirv_magic",
                                     "shader module SPIR-V has an invalid magic word");
    }
    if (spirv_words[1] == 0) {
        return core::Status::failure("renderer.invalid_spirv_version",
                                     "shader module SPIR-V version word must be non-zero");
    }
    if (spirv_words[3] == 0) {
        return core::Status::failure("renderer.invalid_spirv_bound",
                                     "shader module SPIR-V id bound must be non-zero");
    }
    return core::Status::ok();
}

core::Status validate_render_pipeline_layout_shape(const RenderPipelineLayoutDesc& desc) {
    if (!desc.material_id.is_valid()) {
        return core::Status::failure("renderer.invalid_pipeline_material",
                                     "pipeline layout material id must be valid");
    }
    if (!is_valid_virtual_path(desc.shader_template)) {
        return core::Status::failure(
            "renderer.invalid_pipeline_shader_template",
            "pipeline layout shader template must be a valid virtual path");
    }
    if (desc.pipeline_version == 0) {
        return core::Status::failure("renderer.invalid_pipeline_version",
                                     "pipeline layout version must be non-zero");
    }

    std::unordered_set<std::string> names;
    std::unordered_set<std::uint32_t> slots;
    for (const auto& binding : desc.descriptors) {
        if (!is_valid_render_binding_name(binding.name)) {
            return core::Status::failure("renderer.invalid_descriptor_name",
                                         "pipeline descriptor binding name is invalid");
        }
        if (!names.insert(binding.name).second) {
            return core::Status::failure("renderer.duplicate_descriptor_name",
                                         "pipeline descriptor binding name is duplicated");
        }
        if (!slots.insert(binding.slot).second) {
            return core::Status::failure("renderer.duplicate_descriptor_slot",
                                         "pipeline descriptor binding slot is duplicated");
        }
        switch (binding.kind) {
        case RenderDescriptorKind::sampled_texture:
        case RenderDescriptorKind::uniform_scalar:
        case RenderDescriptorKind::uniform_color:
            break;
        }
    }
    return core::Status::ok();
}

core::Status validate_render_compute_pipeline_shape(const RenderComputePipelineDesc& desc) {
    if (!desc.compute_shader.is_valid()) {
        return core::Status::failure("renderer.invalid_compute_shader",
                                     "compute pipeline must reference a valid shader module");
    }
    if (!desc.material_id.is_valid()) {
        return core::Status::failure("renderer.invalid_compute_pipeline_material",
                                     "compute pipeline material id must be valid");
    }
    if (!is_valid_render_binding_name(desc.entry_point)) {
        return core::Status::failure("renderer.invalid_compute_entry_point",
                                     "compute pipeline entry point is invalid");
    }
    return core::Status::ok();
}

core::Status validate_render_graphics_pipeline_shape(const RenderGraphicsPipelineDesc& desc) {
    if (!desc.vertex_shader.is_valid()) {
        return core::Status::failure("renderer.invalid_vertex_shader",
                                     "graphics pipeline must reference a valid vertex shader "
                                     "module");
    }
    if (!desc.fragment_shader.is_valid()) {
        return core::Status::failure("renderer.invalid_fragment_shader",
                                     "graphics pipeline must reference a valid fragment shader "
                                     "module");
    }
    if (desc.vertex_shader.value == desc.fragment_shader.value) {
        return core::Status::failure("renderer.duplicate_graphics_shader",
                                     "graphics pipeline vertex and fragment shader handles must be "
                                     "different");
    }
    if (!desc.material_id.is_valid()) {
        return core::Status::failure("renderer.invalid_graphics_pipeline_material",
                                     "graphics pipeline material id must be valid");
    }
    if (!is_valid_render_binding_name(desc.vertex_entry_point)) {
        return core::Status::failure("renderer.invalid_vertex_entry_point",
                                     "graphics pipeline vertex entry point is invalid");
    }
    if (!is_valid_render_binding_name(desc.fragment_entry_point)) {
        return core::Status::failure("renderer.invalid_fragment_entry_point",
                                     "graphics pipeline fragment entry point is invalid");
    }
    return core::Status::ok();
}

core::Status
validate_render_descriptor_writes_shape(std::span<const RenderDescriptorWrite> writes) {
    if (writes.empty()) {
        return core::Status::failure("renderer.empty_descriptor_write_list",
                                     "descriptor write list must contain at least one write");
    }
    for (const auto& write : writes) {
        if (!write.material_id.is_valid()) {
            return core::Status::failure("renderer.invalid_descriptor_write_material",
                                         "descriptor write material id must be valid");
        }
        if (!is_valid_render_binding_name(write.binding_name)) {
            return core::Status::failure("renderer.invalid_descriptor_write_binding",
                                         "descriptor write binding name is invalid");
        }
        if (!write.resource.is_valid()) {
            return core::Status::failure("renderer.invalid_descriptor_write_resource",
                                         "descriptor write must reference a valid resource handle");
        }
    }
    return core::Status::ok();
}

core::Status validate_render_mesh_bindings_shape(std::span<const RenderMeshBinding> draws) {
    if (draws.empty()) {
        return core::Status::failure("renderer.empty_draw_list",
                                     "mesh draw list must contain at least one draw");
    }
    for (const auto& draw : draws) {
        if (!draw.vertex_buffer.is_valid()) {
            return core::Status::failure("renderer.invalid_vertex_buffer",
                                         "mesh draw must reference a valid vertex buffer");
        }
        if (draw.vertex_count == 0) {
            return core::Status::failure("renderer.invalid_vertex_count",
                                         "mesh draw vertex count must be non-zero");
        }
        if (draw.index_buffer.is_valid() && draw.index_count == 0) {
            return core::Status::failure("renderer.invalid_index_count",
                                         "indexed mesh draw index count must be non-zero");
        }
        if (!draw.index_buffer.is_valid() && draw.index_count != 0) {
            return core::Status::failure("renderer.missing_index_buffer",
                                         "mesh draw with indices must reference an index buffer");
        }
        if (!draw.material_id.is_valid()) {
            return core::Status::failure("renderer.invalid_material",
                                         "mesh draw must reference a valid material prototype id");
        }
        if (draw.instance_count == 0) {
            return core::Status::failure("renderer.invalid_instance_count",
                                         "mesh draw instance count must be non-zero");
        }
    }
    return core::Status::ok();
}

RendererBackendInfo renderer_backend_info(RenderBackend backend) noexcept {
    switch (backend) {
    case RenderBackend::headless:
        return RendererBackendInfo{
            RenderBackend::headless,
            render_backend_name(RenderBackend::headless),
            true,
            "available",
        };
    case RenderBackend::vulkan:
        return heartstead::renderer::vulkan::backend_info();
    }

    return RendererBackendInfo{backend, "unknown", false, "unknown renderer backend"};
}

RenderBackendCapabilities render_backend_capabilities(RenderBackend backend) noexcept {
    const auto info = renderer_backend_info(backend);
    switch (backend) {
    case RenderBackend::headless: {
        RenderBackendCapabilities capabilities;
        capabilities.backend = RenderBackend::headless;
        capabilities.available = info.available;
        capabilities.supports_present = true;
        capabilities.supports_validation = true;
        capabilities.supports_debug_markers = true;
        capabilities.supports_shader_modules = true;
        capabilities.supports_pipeline_layout = true;
        capabilities.supports_compute_pipelines = true;
        capabilities.supports_graphics_pipelines = true;
        capabilities.supports_descriptor_writes = true;
        capabilities.supports_buffer_upload = true;
        capabilities.supports_image_upload = true;
        capabilities.supports_draw_binding = true;
        capabilities.supports_headless = true;
        capabilities.recommended_frames_in_flight = 1;
        capabilities.graphics_api = "headless";
        return capabilities;
    }
    case RenderBackend::vulkan: {
        RenderBackendCapabilities capabilities;
        capabilities.backend = RenderBackend::vulkan;
        capabilities.available = info.available;
        capabilities.supports_present = true;
        capabilities.supports_validation = true;
        capabilities.supports_debug_markers = true;
        capabilities.supports_shader_modules = true;
        capabilities.supports_pipeline_layout = true;
        capabilities.supports_compute_pipelines = true;
        capabilities.supports_graphics_pipelines = true;
        capabilities.supports_descriptor_writes = true;
        capabilities.supports_buffer_upload = true;
        capabilities.supports_image_upload = true;
        capabilities.supports_draw_binding = true;
        capabilities.requires_window_surface = true;
        capabilities.requires_gpu_device = true;
        capabilities.recommended_frames_in_flight = 2;
        capabilities.graphics_api = "vulkan";
        return capabilities;
    }
    }
    RenderBackendCapabilities capabilities;
    capabilities.backend = backend;
    capabilities.graphics_api = "unknown";
    return capabilities;
}

std::string_view render_backend_name(RenderBackend backend) noexcept {
    switch (backend) {
    case RenderBackend::headless:
        return "headless";
    case RenderBackend::vulkan:
        return "vulkan";
    }
    return "unknown";
}

std::string_view present_mode_name(PresentMode mode) noexcept {
    switch (mode) {
    case PresentMode::immediate:
        return "immediate";
    case PresentMode::fifo:
        return "fifo";
    case PresentMode::mailbox:
        return "mailbox";
    }
    return "unknown";
}

std::string_view render_buffer_usage_name(RenderBufferUsage usage) noexcept {
    switch (usage) {
    case RenderBufferUsage::vertex:
        return "vertex";
    case RenderBufferUsage::index:
        return "index";
    case RenderBufferUsage::uniform:
        return "uniform";
    case RenderBufferUsage::storage:
        return "storage";
    }
    return "unknown";
}

std::string_view render_image_format_name(RenderImageFormat format) noexcept {
    switch (format) {
    case RenderImageFormat::rgba8_unorm:
        return "rgba8_unorm";
    }
    return "unknown";
}

std::size_t render_image_format_bytes_per_pixel(RenderImageFormat format) noexcept {
    switch (format) {
    case RenderImageFormat::rgba8_unorm:
        return 4;
    }
    return 0;
}

std::string_view render_descriptor_kind_name(RenderDescriptorKind kind) noexcept {
    switch (kind) {
    case RenderDescriptorKind::sampled_texture:
        return "sampled_texture";
    case RenderDescriptorKind::uniform_scalar:
        return "uniform_scalar";
    case RenderDescriptorKind::uniform_color:
        return "uniform_color";
    }
    return "unknown";
}

std::string_view render_shader_stage_name(RenderShaderStage stage) noexcept {
    switch (stage) {
    case RenderShaderStage::vertex:
        return "vertex";
    case RenderShaderStage::fragment:
        return "fragment";
    case RenderShaderStage::compute:
        return "compute";
    }
    return "unknown";
}

} // namespace heartstead::renderer::rhi
