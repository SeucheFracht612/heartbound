#pragma once

#include "engine/core/result.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace heartstead::core {

struct ReadTextFileOptions {
    std::size_t maximum_bytes = 64U * 1024U * 1024U;
};

struct ReadBinaryFileOptions {
    std::size_t maximum_bytes = 256U * 1024U * 1024U;
};

[[nodiscard]] Result<std::string> read_text_file(const std::filesystem::path& path,
                                                 ReadTextFileOptions options = {});
[[nodiscard]] Result<std::vector<std::uint8_t>>
read_binary_file(const std::filesystem::path& path, ReadBinaryFileOptions options = {});

} // namespace heartstead::core
