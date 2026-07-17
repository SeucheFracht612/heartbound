#pragma once

#include "engine/core/result.hpp"
#include "engine/renderer/assets/render_asset_handles.hpp"
#include "engine/renderer/rhi/render_device.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace heartstead::renderer {

struct ShaderStageAsset {
    rhi::RenderShaderStage stage = rhi::RenderShaderStage::vertex;
    std::string entry_point = "main";
    std::vector<std::uint32_t> spirv;
    std::string source_name;
};

struct ShaderVertexInput {
    std::uint32_t location = 0;
    rhi::RenderVertexAttributeFormat format = rhi::RenderVertexAttributeFormat::float3;

    friend auto operator<=>(const ShaderVertexInput&, const ShaderVertexInput&) = default;
};

struct ShaderDescriptorInterface {
    std::string name;
    rhi::RenderDescriptorKind kind = rhi::RenderDescriptorKind::sampled_texture;
    std::uint32_t slot = 0;
    bool required = true;
    rhi::RenderShaderStageFlags stages =
        rhi::RenderShaderStageFlags::vertex | rhi::RenderShaderStageFlags::fragment;

    friend bool operator==(const ShaderDescriptorInterface&,
                           const ShaderDescriptorInterface&) = default;
};

struct ShaderInterfaceDesc {
    std::uint32_t vertex_stride = 0;
    std::vector<ShaderVertexInput> vertex_inputs;
    std::vector<ShaderDescriptorInterface> descriptors;
    std::vector<rhi::RenderPushConstantRange> push_constant_ranges;
};

struct ShaderProgramDesc {
    std::string id;
    std::vector<ShaderStageAsset> stages;
    ShaderInterfaceDesc interface;
    std::vector<std::string> dependencies;
};

struct ShaderProgramView {
    ShaderProgramHandle handle;
    std::string_view id;
    std::uint64_t revision = 0;
    ShaderInterfaceDesc interface;
    std::span<const std::string> dependencies;
    rhi::RenderResourceHandle vertex_shader;
    rhi::RenderResourceHandle fragment_shader;
    rhi::RenderResourceHandle compute_shader;
    std::string_view vertex_entry_point;
    std::string_view fragment_entry_point;
    std::string_view compute_entry_point;
};

struct ShaderManagerStats {
    std::size_t resident_program_count = 0;
    std::size_t resident_module_count = 0;
    std::size_t superseded_module_count = 0;
    std::uint64_t successful_reload_count = 0;
    std::uint64_t failed_reload_count = 0;
};

class ShaderManager {
  public:
    explicit ShaderManager(rhi::IRenderDevice& device, bool development_hot_reload = false);
    ~ShaderManager();

    ShaderManager(const ShaderManager&) = delete;
    ShaderManager& operator=(const ShaderManager&) = delete;

    [[nodiscard]] core::Result<ShaderProgramHandle> create_program(ShaderProgramDesc desc);
    [[nodiscard]] core::Status reload_program(ShaderProgramHandle handle, ShaderProgramDesc desc);
    [[nodiscard]] core::Status release_program(ShaderProgramHandle handle);
    [[nodiscard]] core::Status release_superseded_modules(ShaderProgramHandle handle);
    [[nodiscard]] core::Status shutdown();

    [[nodiscard]] const ShaderProgramView* find(ShaderProgramHandle handle) const noexcept;
    [[nodiscard]] const ShaderProgramView* find(std::string_view id) const noexcept;
    [[nodiscard]] bool hot_reload_enabled() const noexcept;
    [[nodiscard]] const ShaderManagerStats& stats() const noexcept;

  private:
    struct ProgramRecord;
    struct LoadedProgram;

    [[nodiscard]] core::Result<LoadedProgram> load_program(ShaderProgramDesc desc);
    [[nodiscard]] ProgramRecord* find_record(ShaderProgramHandle handle) noexcept;
    [[nodiscard]] const ProgramRecord* find_record(ShaderProgramHandle handle) const noexcept;
    void refresh_view(ProgramRecord& record);
    void update_stats() noexcept;

    rhi::IRenderDevice& device_;
    bool hot_reload_enabled_ = false;
    // Program views expose addresses and non-owning fields into records; growth must not move them.
    std::deque<ProgramRecord> programs_;
    ShaderManagerStats stats_{};
};

[[nodiscard]] core::Status validate_shader_program_desc(const ShaderProgramDesc& desc);
[[nodiscard]] core::Status
validate_shader_interface(const ShaderProgramView& program,
                          const rhi::RenderPipelineLayoutDesc& layout, std::uint32_t vertex_stride,
                          std::span<const rhi::RenderVertexAttributeDesc> attributes);

} // namespace heartstead::renderer
