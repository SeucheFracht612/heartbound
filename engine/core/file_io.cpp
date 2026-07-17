#include "engine/core/file_io.hpp"

#include <array>
#include <fstream>
#include <utility>

namespace heartstead::core {

Result<std::string> read_text_file(const std::filesystem::path& path, ReadTextFileOptions options) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return Result<std::string>::failure("core.file_open_failed",
                                            "failed to open text file: " + path.string());
    }

    constexpr std::size_t read_buffer_size = 64U * 1024U;
    std::array<char, read_buffer_size> buffer{};
    std::string output;
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto bytes_read = input.gcount();
        if (bytes_read <= 0) {
            continue;
        }
        const auto byte_count = static_cast<std::size_t>(bytes_read);
        if (output.size() > options.maximum_bytes ||
            byte_count > options.maximum_bytes - output.size()) {
            return Result<std::string>::failure("core.file_too_large",
                                                "text file exceeds the " +
                                                    std::to_string(options.maximum_bytes) +
                                                    " byte read limit: " + path.string());
        }
        output.append(buffer.data(), byte_count);
    }
    if (input.bad() || (!input.eof() && input.fail())) {
        return Result<std::string>::failure("core.file_read_failed",
                                            "failed while reading text file: " + path.string());
    }
    return Result<std::string>::success(std::move(output));
}

} // namespace heartstead::core
