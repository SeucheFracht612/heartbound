#pragma once

#include "engine/core/result.hpp"
#include "engine/renderer/materials/material_definition.hpp"
#include "engine/renderer/rhi/render_device.hpp"

#include <cstdint>

namespace heartstead::renderer::materials {

[[nodiscard]] core::Result<rhi::RenderPipelineLayoutDesc>
render_pipeline_layout_from_material(const MaterialDefinition& material,
                                     std::uint32_t pipeline_version = 1);

} // namespace heartstead::renderer::materials
