#include "engine/core/logging.hpp"
#include "engine/world/regions/chunk_region_file.hpp"

#include <charconv>
#include <filesystem>
#include <string_view>
#include <system_error>

namespace {
bool parse_i64(std::string_view text, std::int64_t& output) {
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), output);
    return error == std::errc{} && end == text.data() + text.size();
}
} // namespace

int main(int argc, char** argv) {
    using namespace heartstead;
    if (argc != 5) {
        core::log(core::LogLevel::info, "usage: heartstead_chunk_inspector REGION_ROOT X Y Z");
        return argc == 2 && std::string_view(argv[1]) == "--help" ? 0 : 2;
    }
    world::ChunkRegionCoord coord;
    if (!parse_i64(argv[2], coord.x) || !parse_i64(argv[3], coord.y) ||
        !parse_i64(argv[4], coord.z)) {
        core::log(core::LogLevel::error, "region coordinates must be signed 64-bit integers");
        return 2;
    }
    world::ChunkRegionFileStore store{std::filesystem::path(argv[1])};
    auto region = store.load(coord);
    if (!region) {
        core::log(core::LogLevel::error, region.error().message);
        return 1;
    }
    core::log(core::LogLevel::info, "region=" + std::to_string(coord.x) + "," +
                                        std::to_string(coord.y) + "," + std::to_string(coord.z) +
                                        " chunks=" + std::to_string(region.value().chunks.size()));
    for (const auto& chunk : region.value().chunks) {
        auto global = world::chunk_coord_from_region(coord, chunk.local);
        if (!global) {
            core::log(core::LogLevel::error, global.error().message);
            return 1;
        }
        core::log(core::LogLevel::info,
                  "chunk=" + std::to_string(global.value().x) + "," +
                      std::to_string(global.value().y) + "," + std::to_string(global.value().z) +
                      " generation=" + std::to_string(chunk.generation_stamp) +
                      " revision=" + std::to_string(chunk.save_revision) +
                      " bytes=" + std::to_string(chunk.encoded_chunk.size()));
    }
    return 0;
}
