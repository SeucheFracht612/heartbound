#pragma once

#include "engine/core/result.hpp"
#include "engine/world/coords/world_coords.hpp"

#include <compare>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace heartstead::world {

inline constexpr std::uint8_t chunk_region_edge = 8;
inline constexpr std::size_t chunk_region_capacity = 8U * 8U * 8U;

struct ChunkRegionCoord {
    std::int64_t x = 0;
    std::int64_t y = 0;
    std::int64_t z = 0;

    friend constexpr auto operator<=>(const ChunkRegionCoord&, const ChunkRegionCoord&) = default;
};

struct ChunkRegionLocalCoord {
    std::uint8_t x = 0;
    std::uint8_t y = 0;
    std::uint8_t z = 0;

    friend constexpr auto operator<=>(const ChunkRegionLocalCoord&,
                                      const ChunkRegionLocalCoord&) = default;
};

struct ChunkRegionAddress {
    ChunkRegionCoord region;
    ChunkRegionLocalCoord local;
};

[[nodiscard]] constexpr ChunkRegionAddress chunk_region_address(ChunkCoord chunk) noexcept {
    const auto split_axis = [](std::int64_t value) constexpr {
        constexpr auto edge = static_cast<std::int64_t>(chunk_region_edge);
        auto region = value / edge;
        auto local = value % edge;
        if (local < 0) {
            local += edge;
            --region;
        }
        return std::pair{region, static_cast<std::uint8_t>(local)};
    };
    const auto [rx, lx] = split_axis(chunk.x);
    const auto [ry, ly] = split_axis(chunk.y);
    const auto [rz, lz] = split_axis(chunk.z);
    return {{rx, ry, rz}, {lx, ly, lz}};
}

[[nodiscard]] core::Result<ChunkCoord> chunk_coord_from_region(ChunkRegionCoord region,
                                                               ChunkRegionLocalCoord local);

struct ChunkRegionRecord {
    ChunkRegionLocalCoord local;
    std::uint64_t generation_stamp = 0;
    std::uint64_t save_revision = 0;
    std::string encoded_chunk;
};

struct ChunkRegionFile {
    ChunkRegionCoord coord;
    std::vector<ChunkRegionRecord> chunks;

    [[nodiscard]] core::Status validate() const;
    [[nodiscard]] const ChunkRegionRecord* find(ChunkRegionLocalCoord local) const noexcept;
    [[nodiscard]] core::Status upsert(ChunkRegionRecord record);
};

class ChunkRegionBinaryCodec {
  public:
    [[nodiscard]] static core::Result<std::vector<std::uint8_t>>
    encode(const ChunkRegionFile& region);
    [[nodiscard]] static core::Result<ChunkRegionFile> decode(std::span<const std::uint8_t> bytes);
};

class ChunkRegionFileStore {
  public:
    explicit ChunkRegionFileStore(std::filesystem::path root);

    [[nodiscard]] std::filesystem::path path_for(ChunkRegionCoord coord) const;
    [[nodiscard]] core::Status save(const ChunkRegionFile& region) const;
    [[nodiscard]] core::Result<ChunkRegionFile> load(ChunkRegionCoord coord) const;

  private:
    std::filesystem::path root_;
};

} // namespace heartstead::world
