#pragma once

#include "engine/core/result.hpp"

#include <cstddef>
#include <filesystem>
#include <string>

namespace heartstead::core {

struct ReadTextFileOptions {
    std::size_t maximum_bytes = 64U * 1024U * 1024U;
};

[[nodiscard]] Result<std::string> read_text_file(const std::filesystem::path& path,
                                                 ReadTextFileOptions options = {});

} // namespace heartstead::core
