#include "engine/renderer/shaders/spirv_loader.hpp"

#include <cstring>
#include <fstream>
#include <limits>
#include <string>

namespace heartstead::renderer::shaders {

namespace {

constexpr std::uint32_t spirv_magic = 0x07230203;

} // namespace

core::Status validate_spirv(std::span<const std::uint32_t> words) {
    if (words.size() < 5) {
        return core::Status::failure("renderer.spirv_header_missing",
                                     "SPIR-V binary must contain a complete five-word header");
    }
    if (words[0] != spirv_magic) {
        return core::Status::failure("renderer.invalid_spirv_magic",
                                     "SPIR-V binary has an invalid magic word");
    }
    const auto major_version = (words[1] >> 16U) & 0xffU;
    const auto minor_version = (words[1] >> 8U) & 0xffU;
    if (major_version == 0 || major_version > 1 || minor_version > 6) {
        return core::Status::failure("renderer.unsupported_spirv_version",
                                     "SPIR-V binary version is not supported");
    }
    if (words[3] == 0) {
        return core::Status::failure("renderer.invalid_spirv_bound",
                                     "SPIR-V binary id bound must be non-zero");
    }
    if (words[4] != 0) {
        return core::Status::failure("renderer.invalid_spirv_schema",
                                     "SPIR-V binary schema word must be zero");
    }
    return core::Status::ok();
}

core::Result<std::vector<std::uint32_t>> load_spirv_file(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        return core::Result<std::vector<std::uint32_t>>::failure(
            "renderer.spirv_open_failed", "failed to open SPIR-V file: " + path.string());
    }

    const auto end = stream.tellg();
    if (end < 0) {
        return core::Result<std::vector<std::uint32_t>>::failure(
            "renderer.spirv_size_failed", "failed to determine SPIR-V file size: " + path.string());
    }
    const auto byte_size = static_cast<std::uintmax_t>(end);
    if (byte_size % sizeof(std::uint32_t) != 0 || byte_size < 5 * sizeof(std::uint32_t) ||
        byte_size / sizeof(std::uint32_t) >
            static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max())) {
        return core::Result<std::vector<std::uint32_t>>::failure(
            "renderer.invalid_spirv_size",
            "SPIR-V file size must be a non-empty multiple of four bytes: " + path.string());
    }

    std::vector<std::uint32_t> words(static_cast<std::size_t>(byte_size / sizeof(std::uint32_t)));
    stream.seekg(0, std::ios::beg);
    stream.read(reinterpret_cast<char*>(words.data()), static_cast<std::streamsize>(byte_size));
    if (!stream) {
        return core::Result<std::vector<std::uint32_t>>::failure(
            "renderer.spirv_read_failed", "failed to read complete SPIR-V file: " + path.string());
    }

    auto status = validate_spirv(words);
    if (!status) {
        return core::Result<std::vector<std::uint32_t>>::failure(status.error().code,
                                                                 status.error().message + ": " +
                                                                     path.string());
    }
    return core::Result<std::vector<std::uint32_t>>::success(std::move(words));
}

} // namespace heartstead::renderer::shaders
