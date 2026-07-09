#pragma once

#include "engine/renderer/rhi/render_device.hpp"

namespace heartstead::renderer::vulkan {

[[nodiscard]] rhi::RendererBackendInfo backend_info() noexcept;

[[nodiscard]] core::Result<std::unique_ptr<rhi::IRenderDevice>>
create_device(rhi::RenderDeviceDesc desc);

} // namespace heartstead::renderer::vulkan
