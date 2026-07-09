#pragma once

#include "engine/assets/virtual_file_system.hpp"
#include "engine/core/ids.hpp"
#include "engine/core/result.hpp"
#include "engine/platform/platform.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace heartstead::renderer::rhi {

struct RenderFramePlan;

enum class RenderBackend {
    headless,
    vulkan,
};

enum class PresentMode {
    immediate,
    fifo,
    mailbox,
};

enum class RenderBufferUsage {
    vertex,
    index,
    uniform,
    storage,
};

enum class RenderImageFormat {
    rgba8_unorm,
};

enum class RenderDescriptorKind {
    sampled_texture,
    uniform_scalar,
    uniform_color,
};

enum class RenderShaderStage {
    vertex,
    fragment,
    compute,
};

struct RenderResourceHandle {
    std::uint64_t value = 0;

    [[nodiscard]] bool is_valid() const noexcept {
        return value != 0;
    }
};

struct RenderExtent {
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    [[nodiscard]] bool is_valid() const noexcept;
};

struct ClearColor {
    float red = 0.0F;
    float green = 0.0F;
    float blue = 0.0F;
    float alpha = 1.0F;
};

struct RenderDeviceDesc {
    RenderBackend backend = RenderBackend::headless;
    std::string application_name = "Heartstead";
    RenderExtent initial_extent{1280, 720};
    PresentMode present_mode = PresentMode::fifo;
    bool enable_validation = true;
    std::optional<platform::NativeWindowHandle> native_window;
};

struct RenderFrameDesc {
    ClearColor clear_color{};
    RenderExtent output_extent{};
    bool present = true;
};

struct RenderFrameStats {
    RenderBackend backend = RenderBackend::headless;
    std::uint64_t frame_index = 0;
    RenderExtent extent{};
    ClearColor clear_color{};
    bool presented = false;
    std::size_t render_pass_count = 0;
    std::size_t present_pass_count = 0;
    std::size_t resource_use_count = 0;
    std::size_t dependency_count = 0;
    std::size_t transition_count = 0;
    std::size_t synchronization_barrier_count = 0;
    std::size_t submitted_synchronization_barrier_count = 0;
};

struct RenderBufferDesc {
    RenderBufferUsage usage = RenderBufferUsage::vertex;
    std::size_t byte_size = 0;
    std::string debug_name;
};

struct RenderImageDesc {
    RenderImageFormat format = RenderImageFormat::rgba8_unorm;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::string debug_name;
};

struct RenderUploadStats {
    RenderBackend backend = RenderBackend::headless;
    RenderResourceHandle handle;
    RenderBufferUsage usage = RenderBufferUsage::vertex;
    std::size_t byte_size = 0;
    std::size_t live_resource_count = 0;
    bool gpu_backed = false;
};

struct RenderImageUploadStats {
    RenderBackend backend = RenderBackend::headless;
    RenderResourceHandle handle;
    RenderImageFormat format = RenderImageFormat::rgba8_unorm;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::size_t byte_size = 0;
    std::size_t live_resource_count = 0;
    bool gpu_backed = false;
};

struct RenderDescriptorBinding {
    std::string name;
    RenderDescriptorKind kind = RenderDescriptorKind::sampled_texture;
    std::uint32_t slot = 0;
    bool required = true;
};

struct RenderPipelineLayoutDesc {
    core::PrototypeId material_id;
    assets::VirtualPath shader_template;
    std::uint32_t pipeline_version = 1;
    std::vector<RenderDescriptorBinding> descriptors;
    std::string debug_name;
};

struct RenderPipelineLayoutStats {
    RenderBackend backend = RenderBackend::headless;
    core::PrototypeId material_id;
    std::uint32_t pipeline_version = 0;
    std::size_t descriptor_count = 0;
    std::size_t sampled_texture_count = 0;
    std::size_t uniform_count = 0;
    std::size_t bound_pipeline_count = 0;
    bool gpu_backed = false;
};

struct RenderShaderModuleDesc {
    RenderShaderStage stage = RenderShaderStage::vertex;
    std::string debug_name;
};

struct RenderShaderModuleStats {
    RenderBackend backend = RenderBackend::headless;
    RenderResourceHandle handle;
    RenderShaderStage stage = RenderShaderStage::vertex;
    std::size_t word_count = 0;
    std::size_t live_shader_module_count = 0;
    bool gpu_backed = false;
};

struct RenderComputePipelineDesc {
    RenderResourceHandle compute_shader;
    core::PrototypeId material_id;
    std::string entry_point = "main";
    std::string debug_name;
};

struct RenderComputePipelineStats {
    RenderBackend backend = RenderBackend::headless;
    RenderResourceHandle handle;
    RenderResourceHandle compute_shader;
    core::PrototypeId material_id;
    std::size_t live_compute_pipeline_count = 0;
    bool gpu_backed = false;
};

struct RenderGraphicsPipelineDesc {
    RenderResourceHandle vertex_shader;
    RenderResourceHandle fragment_shader;
    core::PrototypeId material_id;
    std::string vertex_entry_point = "main";
    std::string fragment_entry_point = "main";
    std::string debug_name;
};

struct RenderGraphicsPipelineStats {
    RenderBackend backend = RenderBackend::headless;
    RenderResourceHandle handle;
    RenderResourceHandle vertex_shader;
    RenderResourceHandle fragment_shader;
    core::PrototypeId material_id;
    std::size_t live_graphics_pipeline_count = 0;
    bool gpu_backed = false;
};

struct RenderDescriptorWrite {
    core::PrototypeId material_id;
    std::string binding_name;
    RenderResourceHandle resource;
    std::size_t byte_offset = 0;
    std::size_t byte_size = 0;
};

struct RenderDescriptorWriteStats {
    RenderBackend backend = RenderBackend::headless;
    std::size_t write_count = 0;
    std::size_t uniform_write_count = 0;
    std::size_t sampled_texture_write_count = 0;
    std::size_t material_count = 0;
    bool gpu_backed = false;
};

struct RenderMeshBinding {
    RenderResourceHandle vertex_buffer;
    RenderResourceHandle index_buffer;
    core::PrototypeId material_id;
    std::uint32_t vertex_count = 0;
    std::uint32_t index_count = 0;
    std::uint32_t instance_count = 1;
    std::string debug_name;
};

struct RenderDrawStats {
    RenderBackend backend = RenderBackend::headless;
    std::size_t draw_count = 0;
    std::size_t indexed_draw_count = 0;
    std::size_t material_count = 0;
    std::size_t total_vertices = 0;
    std::size_t total_indices = 0;
    bool gpu_backed = false;
    bool draw_commands_submitted = false;
};

struct RendererBackendInfo {
    RenderBackend backend = RenderBackend::headless;
    std::string_view name;
    bool available = false;
    std::string_view status;
};

struct RenderDeviceCapabilities {
    RenderBackend backend = RenderBackend::headless;
    RenderExtent max_extent{};
    bool supports_present = false;
    bool supports_validation = false;
    bool supports_debug_markers = false;
    bool supports_shader_modules = false;
    bool supports_pipeline_layout = false;
    bool supports_compute_pipelines = false;
    bool supports_graphics_pipelines = false;
    bool supports_descriptor_writes = false;
    bool supports_buffer_upload = false;
    bool supports_image_upload = false;
    bool supports_draw_binding = false;
    bool headless = true;
};

struct RenderBackendCapabilities {
    RenderBackend backend = RenderBackend::headless;
    bool available = false;
    bool supports_present = false;
    bool supports_validation = false;
    bool supports_debug_markers = false;
    bool supports_shader_modules = false;
    bool supports_pipeline_layout = false;
    bool supports_compute_pipelines = false;
    bool supports_graphics_pipelines = false;
    bool supports_descriptor_writes = false;
    bool supports_buffer_upload = false;
    bool supports_image_upload = false;
    bool supports_draw_binding = false;
    bool requires_window_surface = false;
    bool requires_gpu_device = false;
    bool supports_headless = false;
    std::uint32_t recommended_frames_in_flight = 1;
    std::string_view graphics_api;
};

class IRenderDevice {
  public:
    virtual ~IRenderDevice() = default;

    [[nodiscard]] virtual RenderBackend backend() const noexcept = 0;
    [[nodiscard]] virtual std::string_view backend_name() const noexcept = 0;
    [[nodiscard]] virtual RenderDeviceCapabilities capabilities() const noexcept = 0;
    [[nodiscard]] virtual RenderExtent current_extent() const noexcept = 0;
    [[nodiscard]] virtual std::uint64_t completed_frame_count() const noexcept = 0;
    [[nodiscard]] virtual std::size_t live_resource_count() const noexcept = 0;

    [[nodiscard]] virtual core::Status resize(RenderExtent extent) = 0;
    [[nodiscard]] virtual core::Result<RenderFrameStats> render_frame(RenderFrameDesc desc) = 0;
    [[nodiscard]] virtual core::Result<RenderFrameStats>
    execute_frame_plan(const RenderFramePlan& plan) = 0;
    [[nodiscard]] virtual core::Result<RenderUploadStats>
    upload_buffer(RenderBufferDesc desc, std::span<const std::byte> bytes) = 0;
    [[nodiscard]] virtual core::Result<RenderImageUploadStats>
    upload_image(RenderImageDesc desc, std::span<const std::byte> bytes) = 0;
    [[nodiscard]] virtual core::Result<RenderShaderModuleStats>
    create_shader_module(RenderShaderModuleDesc desc,
                         std::span<const std::uint32_t> spirv_words) = 0;
    [[nodiscard]] virtual core::Result<RenderPipelineLayoutStats>
    bind_pipeline_layout(RenderPipelineLayoutDesc desc) = 0;
    [[nodiscard]] virtual core::Result<RenderComputePipelineStats>
    create_compute_pipeline(RenderComputePipelineDesc desc) = 0;
    [[nodiscard]] virtual core::Result<RenderGraphicsPipelineStats>
    create_graphics_pipeline(RenderGraphicsPipelineDesc desc) = 0;
    [[nodiscard]] virtual core::Result<RenderDescriptorWriteStats>
    write_descriptors(std::span<const RenderDescriptorWrite> writes) = 0;
    [[nodiscard]] virtual core::Result<RenderDrawStats>
    bind_mesh_draws(std::span<const RenderMeshBinding> draws) = 0;
    [[nodiscard]] virtual core::Status release_resource(RenderResourceHandle handle) = 0;
};

[[nodiscard]] core::Result<std::unique_ptr<IRenderDevice>>
create_render_device(RenderDeviceDesc desc);

[[nodiscard]] core::Status validate_render_device_desc(const RenderDeviceDesc& desc);
[[nodiscard]] core::Status validate_render_extent(RenderExtent extent);
[[nodiscard]] core::Status validate_render_buffer_upload(const RenderBufferDesc& desc,
                                                         std::span<const std::byte> bytes);
[[nodiscard]] core::Status validate_render_image_upload(const RenderImageDesc& desc,
                                                        std::span<const std::byte> bytes);
[[nodiscard]] core::Status
validate_render_shader_module_upload(const RenderShaderModuleDesc& desc,
                                     std::span<const std::uint32_t> spirv_words);
[[nodiscard]] core::Status
validate_render_pipeline_layout_shape(const RenderPipelineLayoutDesc& desc);
[[nodiscard]] core::Status
validate_render_compute_pipeline_shape(const RenderComputePipelineDesc& desc);
[[nodiscard]] core::Status
validate_render_graphics_pipeline_shape(const RenderGraphicsPipelineDesc& desc);
[[nodiscard]] core::Status
validate_render_descriptor_writes_shape(std::span<const RenderDescriptorWrite> writes);
[[nodiscard]] core::Status
validate_render_mesh_bindings_shape(std::span<const RenderMeshBinding> draws);

[[nodiscard]] RendererBackendInfo renderer_backend_info(RenderBackend backend) noexcept;
[[nodiscard]] RenderBackendCapabilities render_backend_capabilities(RenderBackend backend) noexcept;
[[nodiscard]] std::string_view render_backend_name(RenderBackend backend) noexcept;
[[nodiscard]] std::string_view present_mode_name(PresentMode mode) noexcept;
[[nodiscard]] std::string_view render_buffer_usage_name(RenderBufferUsage usage) noexcept;
[[nodiscard]] std::string_view render_image_format_name(RenderImageFormat format) noexcept;
[[nodiscard]] std::size_t render_image_format_bytes_per_pixel(RenderImageFormat format) noexcept;
[[nodiscard]] std::string_view render_descriptor_kind_name(RenderDescriptorKind kind) noexcept;
[[nodiscard]] std::string_view render_shader_stage_name(RenderShaderStage stage) noexcept;

} // namespace heartstead::renderer::rhi
