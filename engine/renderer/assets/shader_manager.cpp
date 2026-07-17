#include "engine/renderer/assets/shader_manager.hpp"

#include "engine/renderer/shaders/spirv_loader.hpp"

#include <algorithm>
#include <exception>
#include <limits>
#include <ranges>
#include <unordered_set>
#include <utility>

namespace heartstead::renderer {

namespace {

[[nodiscard]] bool range_contains(const rhi::RenderPushConstantRange& outer,
                                  const rhi::RenderPushConstantRange& inner) noexcept {
    if ((outer.stages & inner.stages) != inner.stages || outer.byte_offset > inner.byte_offset) {
        return false;
    }
    const auto outer_end = static_cast<std::uint64_t>(outer.byte_offset) + outer.byte_size;
    const auto inner_end = static_cast<std::uint64_t>(inner.byte_offset) + inner.byte_size;
    return outer_end >= inner_end;
}

} // namespace

struct ShaderManager::LoadedProgram {
    ShaderProgramDesc desc;
    std::vector<rhi::RenderResourceHandle> modules;
    rhi::RenderResourceHandle vertex_shader;
    rhi::RenderResourceHandle fragment_shader;
    rhi::RenderResourceHandle compute_shader;
    std::string vertex_entry_point;
    std::string fragment_entry_point;
    std::string compute_entry_point;
};

struct ShaderManager::ProgramRecord {
    std::uint32_t generation = 1;
    bool occupied = false;
    std::uint64_t revision = 0;
    LoadedProgram loaded;
    std::vector<rhi::RenderResourceHandle> superseded_modules;
    ShaderProgramView view;
};

ShaderManager::ShaderManager(rhi::IRenderDevice& device, bool development_hot_reload)
    : device_(device), hot_reload_enabled_(development_hot_reload) {}

ShaderManager::~ShaderManager() {
    (void)shutdown();
}

core::Result<ShaderProgramHandle> ShaderManager::create_program(ShaderProgramDesc desc) {
    if (find(desc.id) != nullptr) {
        return core::Result<ShaderProgramHandle>::failure(
            "shader_manager.duplicate_program",
            "shader program id is already resident: " + desc.id);
    }
    auto loaded = load_program(std::move(desc));
    if (!loaded) {
        return core::Result<ShaderProgramHandle>::failure(loaded.error().code,
                                                          loaded.error().message);
    }

    std::size_t slot = programs_.size();
    for (std::size_t index = 0; index < programs_.size(); ++index) {
        if (!programs_[index].occupied) {
            slot = index;
            break;
        }
    }
    if (slot == programs_.size()) {
        programs_.emplace_back();
    }
    auto& record = programs_[slot];
    record.superseded_modules.clear();
    record.occupied = true;
    record.revision = 1;
    record.loaded = std::move(loaded).value();
    record.view.handle = {static_cast<std::uint32_t>(slot + 1), record.generation};
    refresh_view(record);
    update_stats();
    return core::Result<ShaderProgramHandle>::success(record.view.handle);
}

core::Status ShaderManager::reload_program(ShaderProgramHandle handle, ShaderProgramDesc desc) {
    auto* record = find_record(handle);
    if (record == nullptr) {
        return core::Status::failure("shader_manager.stale_program_handle",
                                     "shader reload references a stale program handle");
    }
    if (!hot_reload_enabled_) {
        return core::Status::failure("shader_manager.hot_reload_disabled",
                                     "shader hot reload is disabled for this renderer");
    }
    if (desc.id != record->loaded.desc.id) {
        return core::Status::failure("shader_manager.reload_id_mismatch",
                                     "shader reload id must match the resident program");
    }

    auto replacement = load_program(std::move(desc));
    if (!replacement) {
        ++stats_.failed_reload_count;
        return core::Status::failure(replacement.error().code, replacement.error().message);
    }
    record->superseded_modules.insert(record->superseded_modules.end(),
                                      record->loaded.modules.begin(), record->loaded.modules.end());
    record->loaded = std::move(replacement).value();
    if (record->revision == std::numeric_limits<std::uint64_t>::max()) {
        std::terminate();
    }
    ++record->revision;
    ++stats_.successful_reload_count;
    refresh_view(*record);
    update_stats();
    return core::Status::ok();
}

core::Status ShaderManager::release_program(ShaderProgramHandle handle) {
    auto* record = find_record(handle);
    if (record == nullptr) {
        return core::Status::failure("shader_manager.stale_program_handle",
                                     "shader release references a stale program handle");
    }
    core::Status first_failure = core::Status::ok();
    for (const auto module : record->loaded.modules) {
        auto status = device_.release_resource(module);
        if (!status && first_failure) {
            first_failure = status;
        }
    }
    for (const auto module : record->superseded_modules) {
        auto status = device_.release_resource(module);
        if (!status && first_failure) {
            first_failure = status;
        }
    }
    record->superseded_modules.clear();
    record->loaded = {};
    record->view = {};
    record->occupied = false;
    record->revision = 0;
    if (record->generation == std::numeric_limits<std::uint32_t>::max()) {
        std::terminate();
    }
    ++record->generation;
    update_stats();
    return first_failure;
}

core::Status ShaderManager::release_superseded_modules(ShaderProgramHandle handle) {
    auto* record = find_record(handle);
    if (record == nullptr) {
        return core::Status::failure("shader_manager.stale_program_handle",
                                     "shader retirement references a stale program handle");
    }
    core::Status first_failure = core::Status::ok();
    for (const auto module : record->superseded_modules) {
        auto status = device_.release_resource(module);
        if (!status && first_failure) {
            first_failure = status;
        }
    }
    record->superseded_modules.clear();
    update_stats();
    return first_failure;
}

core::Status ShaderManager::shutdown() {
    core::Status first_failure = core::Status::ok();
    for (auto& record : programs_) {
        if (!record.occupied) {
            continue;
        }
        for (const auto module : record.loaded.modules) {
            auto status = device_.release_resource(module);
            if (!status && first_failure) {
                first_failure = status;
            }
        }
        for (const auto module : record.superseded_modules) {
            auto status = device_.release_resource(module);
            if (!status && first_failure) {
                first_failure = status;
            }
        }
        record = {};
    }
    programs_.clear();
    update_stats();
    return first_failure;
}

const ShaderProgramView* ShaderManager::find(ShaderProgramHandle handle) const noexcept {
    const auto* record = find_record(handle);
    return record == nullptr ? nullptr : &record->view;
}

const ShaderProgramView* ShaderManager::find(std::string_view id) const noexcept {
    const auto found = std::ranges::find_if(programs_, [id](const ProgramRecord& record) {
        return record.occupied && record.loaded.desc.id == id;
    });
    return found == programs_.end() ? nullptr : &found->view;
}

bool ShaderManager::hot_reload_enabled() const noexcept {
    return hot_reload_enabled_;
}

const ShaderManagerStats& ShaderManager::stats() const noexcept {
    return stats_;
}

core::Result<ShaderManager::LoadedProgram> ShaderManager::load_program(ShaderProgramDesc desc) {
    auto status = validate_shader_program_desc(desc);
    if (!status) {
        return core::Result<LoadedProgram>::failure(status.error().code, status.error().message);
    }

    LoadedProgram loaded;
    loaded.desc = std::move(desc);
    loaded.modules.reserve(loaded.desc.stages.size());
    for (const auto& stage : loaded.desc.stages) {
        auto created = device_.create_shader_module(
            {stage.stage,
             loaded.desc.id + ":" + std::string(rhi::render_shader_stage_name(stage.stage))},
            stage.spirv);
        if (!created) {
            for (const auto module : loaded.modules) {
                (void)device_.release_resource(module);
            }
            return core::Result<LoadedProgram>::failure(created.error().code,
                                                        created.error().message);
        }
        loaded.modules.push_back(created.value().handle);
        switch (stage.stage) {
        case rhi::RenderShaderStage::vertex:
            loaded.vertex_shader = created.value().handle;
            loaded.vertex_entry_point = stage.entry_point;
            break;
        case rhi::RenderShaderStage::fragment:
            loaded.fragment_shader = created.value().handle;
            loaded.fragment_entry_point = stage.entry_point;
            break;
        case rhi::RenderShaderStage::compute:
            loaded.compute_shader = created.value().handle;
            loaded.compute_entry_point = stage.entry_point;
            break;
        }
    }
    return core::Result<LoadedProgram>::success(std::move(loaded));
}

ShaderManager::ProgramRecord* ShaderManager::find_record(ShaderProgramHandle handle) noexcept {
    if (!handle.is_valid() || handle.index > programs_.size()) {
        return nullptr;
    }
    auto& record = programs_[handle.index - 1];
    return record.occupied && record.generation == handle.generation ? &record : nullptr;
}

const ShaderManager::ProgramRecord*
ShaderManager::find_record(ShaderProgramHandle handle) const noexcept {
    if (!handle.is_valid() || handle.index > programs_.size()) {
        return nullptr;
    }
    const auto& record = programs_[handle.index - 1];
    return record.occupied && record.generation == handle.generation ? &record : nullptr;
}

void ShaderManager::refresh_view(ProgramRecord& record) {
    record.view.id = record.loaded.desc.id;
    record.view.revision = record.revision;
    record.view.interface = record.loaded.desc.interface;
    record.view.dependencies = record.loaded.desc.dependencies;
    record.view.vertex_shader = record.loaded.vertex_shader;
    record.view.fragment_shader = record.loaded.fragment_shader;
    record.view.compute_shader = record.loaded.compute_shader;
    record.view.vertex_entry_point = record.loaded.vertex_entry_point;
    record.view.fragment_entry_point = record.loaded.fragment_entry_point;
    record.view.compute_entry_point = record.loaded.compute_entry_point;
}

void ShaderManager::update_stats() noexcept {
    stats_.resident_program_count = static_cast<std::size_t>(std::ranges::count_if(
        programs_, [](const ProgramRecord& record) { return record.occupied; }));
    stats_.resident_module_count = 0;
    for (const auto& record : programs_) {
        if (record.occupied) {
            stats_.resident_module_count += record.loaded.modules.size();
        }
    }
    stats_.superseded_module_count = 0;
    for (const auto& record : programs_) {
        if (record.occupied) {
            stats_.superseded_module_count += record.superseded_modules.size();
        }
    }
}

core::Status validate_shader_program_desc(const ShaderProgramDesc& desc) {
    if (desc.id.empty()) {
        return core::Status::failure("shader_manager.missing_program_id",
                                     "shader program id must not be empty");
    }
    if (desc.stages.empty()) {
        return core::Status::failure("shader_manager.missing_stages",
                                     "shader program must contain at least one stage");
    }
    std::unordered_set<std::uint32_t> stages;
    for (const auto& stage : desc.stages) {
        if (stage.entry_point.empty()) {
            return core::Status::failure("shader_manager.missing_entry_point",
                                         "shader stage entry point must not be empty");
        }
        if (!stages.insert(static_cast<std::uint32_t>(stage.stage)).second) {
            return core::Status::failure("shader_manager.duplicate_stage",
                                         "shader program contains a duplicate stage");
        }
        auto status = shaders::validate_spirv(stage.spirv);
        if (!status) {
            return core::Status::failure(status.error().code,
                                         stage.source_name + ": " + status.error().message);
        }
    }
    const bool has_compute =
        stages.contains(static_cast<std::uint32_t>(rhi::RenderShaderStage::compute));
    const bool has_vertex =
        stages.contains(static_cast<std::uint32_t>(rhi::RenderShaderStage::vertex));
    const bool has_fragment =
        stages.contains(static_cast<std::uint32_t>(rhi::RenderShaderStage::fragment));
    if (has_compute && stages.size() != 1) {
        return core::Status::failure("shader_manager.mixed_compute_graphics",
                                     "compute shader programs cannot include graphics stages");
    }
    if (!has_compute && (!has_vertex || !has_fragment)) {
        return core::Status::failure("shader_manager.incomplete_graphics_program",
                                     "graphics shader programs require vertex and fragment stages");
    }
    const auto available_stage_bits =
        (has_vertex ? static_cast<std::uint32_t>(rhi::RenderShaderStageFlags::vertex) : 0U) |
        (has_fragment ? static_cast<std::uint32_t>(rhi::RenderShaderStageFlags::fragment) : 0U) |
        (has_compute ? static_cast<std::uint32_t>(rhi::RenderShaderStageFlags::compute) : 0U);
    for (const auto& descriptor : desc.interface.descriptors) {
        const auto descriptor_stage_bits = static_cast<std::uint32_t>(descriptor.stages);
        if (descriptor_stage_bits == 0 ||
            (descriptor_stage_bits & ~available_stage_bits) != 0) {
            return core::Status::failure(
                "shader_manager.invalid_descriptor_stages",
                "shader descriptor stages must be non-empty stages present in the program");
        }
    }
    return core::Status::ok();
}

core::Status validate_shader_interface(const ShaderProgramView& program,
                                       const rhi::RenderPipelineLayoutDesc& layout,
                                       std::uint32_t vertex_stride,
                                       std::span<const rhi::RenderVertexAttributeDesc> attributes) {
    const auto& interface = program.interface;
    if (interface.vertex_stride != vertex_stride) {
        return core::Status::failure("shader_manager.vertex_stride_mismatch",
                                     "pipeline vertex stride does not match shader metadata");
    }
    if (interface.vertex_inputs.size() != attributes.size()) {
        return core::Status::failure("shader_manager.vertex_input_count_mismatch",
                                     "pipeline vertex input count does not match shader metadata");
    }
    for (const auto& input : interface.vertex_inputs) {
        const auto found = std::ranges::find_if(attributes, [&input](const auto& attribute) {
            return attribute.location == input.location && attribute.format == input.format;
        });
        if (found == attributes.end()) {
            return core::Status::failure("shader_manager.vertex_input_mismatch",
                                         "pipeline vertex input does not match shader metadata");
        }
    }
    for (const auto& expected : interface.descriptors) {
        const auto found =
            std::ranges::find_if(layout.descriptors, [&expected](const auto& binding) {
                const auto binding_stages = static_cast<std::uint32_t>(binding.stages);
                const auto expected_stages = static_cast<std::uint32_t>(expected.stages);
                return binding.name == expected.name && binding.kind == expected.kind &&
                       binding.slot == expected.slot &&
                       (binding_stages & expected_stages) == expected_stages;
            });
        if (found == layout.descriptors.end() && expected.required) {
            return core::Status::failure(
                "shader_manager.descriptor_interface_mismatch",
                "required shader descriptor is missing from pipeline layout: " + expected.name);
        }
    }
    for (const auto& expected : interface.push_constant_ranges) {
        const auto found =
            std::ranges::find_if(layout.push_constant_ranges, [&expected](const auto& range) {
                return range_contains(range, expected);
            });
        if (found == layout.push_constant_ranges.end()) {
            return core::Status::failure(
                "shader_manager.push_constant_interface_mismatch",
                "shader push-constant range is absent from pipeline layout");
        }
    }
    return core::Status::ok();
}

} // namespace heartstead::renderer
