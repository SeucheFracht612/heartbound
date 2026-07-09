#include "engine/renderer/materials/material_pipeline_layout.hpp"

#include <utility>

namespace heartstead::renderer::materials {

core::Result<rhi::RenderPipelineLayoutDesc>
render_pipeline_layout_from_material(const MaterialDefinition& material,
                                     std::uint32_t pipeline_version) {
    auto status = validate_material_definition(material);
    if (!status) {
        return core::Result<rhi::RenderPipelineLayoutDesc>::failure(status.error().code,
                                                                    status.error().message);
    }

    rhi::RenderPipelineLayoutDesc layout;
    layout.material_id = material.id;
    layout.shader_template = material.shader_template;
    layout.pipeline_version = pipeline_version;
    layout.debug_name = material.id.value();

    std::uint32_t slot = 0;
    for (const auto& texture : material.textures) {
        layout.descriptors.push_back(rhi::RenderDescriptorBinding{
            texture.name,
            rhi::RenderDescriptorKind::sampled_texture,
            slot++,
            texture.required,
        });
    }
    for (const auto& scalar : material.scalars) {
        layout.descriptors.push_back(rhi::RenderDescriptorBinding{
            scalar.name,
            rhi::RenderDescriptorKind::uniform_scalar,
            slot++,
            true,
        });
    }
    for (const auto& color : material.colors) {
        layout.descriptors.push_back(rhi::RenderDescriptorBinding{
            color.name,
            rhi::RenderDescriptorKind::uniform_color,
            slot++,
            true,
        });
    }

    status = rhi::validate_render_pipeline_layout_shape(layout);
    if (!status) {
        return core::Result<rhi::RenderPipelineLayoutDesc>::failure(status.error().code,
                                                                    status.error().message);
    }
    return core::Result<rhi::RenderPipelineLayoutDesc>::success(std::move(layout));
}

} // namespace heartstead::renderer::materials
