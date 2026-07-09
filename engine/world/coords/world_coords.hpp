#pragma once

#include "engine/core/result.hpp"

#include <compare>
#include <cstdint>
#include <limits>

namespace heartstead::world {

inline constexpr std::uint16_t chunk_edge_length = 32;

struct BlockCoord {
    std::int64_t x = 0;
    std::int64_t y = 0;
    std::int64_t z = 0;

    friend constexpr auto operator<=>(const BlockCoord&, const BlockCoord&) = default;
};

struct ChunkCoord {
    std::int64_t x = 0;
    std::int64_t y = 0;
    std::int64_t z = 0;

    friend constexpr auto operator<=>(const ChunkCoord&, const ChunkCoord&) = default;
};

struct LocalCoord {
    std::uint16_t x = 0;
    std::uint16_t y = 0;
    std::uint16_t z = 0;

    friend constexpr auto operator<=>(const LocalCoord&, const LocalCoord&) = default;
};

using VoxelCoord = LocalCoord;

struct ChunkLocalCoord {
    ChunkCoord chunk;
    LocalCoord local;

    friend constexpr auto operator<=>(const ChunkLocalCoord&, const ChunkLocalCoord&) = default;
};

[[nodiscard]] inline core::Result<std::int64_t> floor_div_i64_checked(std::int64_t dividend,
                                                                      std::int64_t divisor) {
    if (divisor == 0) {
        return core::Result<std::int64_t>::failure("world_coord.divide_by_zero",
                                                   "floor division divisor cannot be zero");
    }
    if (dividend == std::numeric_limits<std::int64_t>::min() && divisor == -1) {
        return core::Result<std::int64_t>::failure("world_coord.floor_div_overflow",
                                                   "floor division result does not fit in int64");
    }

    auto quotient = dividend / divisor;
    const auto remainder = dividend % divisor;
    if (remainder != 0 && ((remainder < 0) != (divisor < 0))) {
        --quotient;
    }
    return core::Result<std::int64_t>::success(quotient);
}

[[nodiscard]] constexpr std::int64_t chunk_axis_for_block(std::int64_t block_axis) noexcept {
    constexpr auto edge = static_cast<std::int64_t>(chunk_edge_length);
    auto quotient = block_axis / edge;
    if (block_axis % edge < 0) {
        --quotient;
    }
    return quotient;
}

[[nodiscard]] constexpr std::uint16_t local_axis_for_block(std::int64_t block_axis) noexcept {
    constexpr auto edge = static_cast<std::int64_t>(chunk_edge_length);
    auto remainder = block_axis % edge;
    if (remainder < 0) {
        remainder += edge;
    }
    return static_cast<std::uint16_t>(remainder);
}

[[nodiscard]] constexpr ChunkCoord chunk_coord_for_block(BlockCoord block) noexcept {
    return {
        chunk_axis_for_block(block.x),
        chunk_axis_for_block(block.y),
        chunk_axis_for_block(block.z),
    };
}

[[nodiscard]] constexpr LocalCoord local_coord_for_block(BlockCoord block) noexcept {
    return {
        local_axis_for_block(block.x),
        local_axis_for_block(block.y),
        local_axis_for_block(block.z),
    };
}

[[nodiscard]] constexpr ChunkLocalCoord block_to_chunk_local(BlockCoord block) noexcept {
    return {chunk_coord_for_block(block), local_coord_for_block(block)};
}

[[nodiscard]] constexpr bool is_valid_local_coord(LocalCoord local) noexcept {
    return local.x < chunk_edge_length && local.y < chunk_edge_length &&
           local.z < chunk_edge_length;
}

namespace detail {

[[nodiscard]] inline core::Result<std::int64_t>
block_axis_for_chunk_local(std::int64_t chunk_axis, std::uint16_t local_axis) {
    if (local_axis >= chunk_edge_length) {
        return core::Result<std::int64_t>::failure("world_coord.invalid_local_coord",
                                                   "local coordinate is outside the 32-cell chunk");
    }

    constexpr auto edge = static_cast<std::int64_t>(chunk_edge_length);
    constexpr auto min = std::numeric_limits<std::int64_t>::min();
    constexpr auto max = std::numeric_limits<std::int64_t>::max();
    const auto local = static_cast<std::int64_t>(local_axis);
    if (chunk_axis < min / edge || chunk_axis > (max - local) / edge) {
        return core::Result<std::int64_t>::failure(
            "world_coord.block_coord_overflow",
            "chunk and local coordinates do not fit in a signed 64-bit block coordinate");
    }
    return core::Result<std::int64_t>::success(chunk_axis * edge + local);
}

} // namespace detail

[[nodiscard]] inline core::Result<BlockCoord> chunk_local_to_block(ChunkCoord chunk,
                                                                   LocalCoord local) {
    if (!is_valid_local_coord(local)) {
        return core::Result<BlockCoord>::failure("world_coord.invalid_local_coord",
                                                 "local coordinate is outside the 32-cell chunk");
    }

    auto x = detail::block_axis_for_chunk_local(chunk.x, local.x);
    if (!x) {
        return core::Result<BlockCoord>::failure(x.error().code, x.error().message);
    }
    auto y = detail::block_axis_for_chunk_local(chunk.y, local.y);
    if (!y) {
        return core::Result<BlockCoord>::failure(y.error().code, y.error().message);
    }
    auto z = detail::block_axis_for_chunk_local(chunk.z, local.z);
    if (!z) {
        return core::Result<BlockCoord>::failure(z.error().code, z.error().message);
    }
    return core::Result<BlockCoord>::success({x.value(), y.value(), z.value()});
}

} // namespace heartstead::world
