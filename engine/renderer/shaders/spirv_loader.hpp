#pragma once

#include "engine/core/result.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace heartstead::renderer::shaders {

[[nodiscard]] core::Status validate_spirv(std::span<const std::uint32_t> words);
[[nodiscard]] core::Result<std::vector<std::uint32_t>>
load_spirv_file(const std::filesystem::path& path);

} // namespace heartstead::renderer::shaders
