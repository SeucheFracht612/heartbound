#pragma once

#include "engine/world/coords/world_coords.hpp"

#include <compare>
#include <cstdint>

namespace heartstead::world {

// Coordinates identify a location. The generation identifies one particular residency at that
// location and is never reused during the process lifetime.
struct ChunkIdentity {
    ChunkCoord coordinate;
    std::uint64_t load_generation = 0;

    [[nodiscard]] constexpr bool is_valid() const noexcept {
        return load_generation != 0;
    }

    friend auto operator<=>(const ChunkIdentity&, const ChunkIdentity&) = default;
};

} // namespace heartstead::world
