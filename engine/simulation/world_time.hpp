#pragma once

#include "engine/core/result.hpp"

#include <cstdint>

namespace heartstead::simulation {

using WorldTick = std::uint64_t;

struct WorldTimeConfig {
    std::uint32_t ticks_per_second = 20;
    std::uint32_t seconds_per_minute = 60;
    std::uint32_t minutes_per_hour = 60;
    std::uint32_t hours_per_day = 24;

    [[nodiscard]] core::Status validate() const;
    [[nodiscard]] core::Result<WorldTick> ticks_per_hour() const;
    [[nodiscard]] core::Result<WorldTick> ticks_per_day() const;
};

class WorldClock {
  public:
    explicit constexpr WorldClock(WorldTick initial_time = 0) noexcept : now_(initial_time) {}

    [[nodiscard]] constexpr WorldTick now() const noexcept {
        return now_;
    }

    [[nodiscard]] core::Status advance(WorldTick delta) noexcept;
    [[nodiscard]] core::Status advance_hours(std::uint64_t hours,
                                             const WorldTimeConfig& config) noexcept;

  private:
    WorldTick now_ = 0;
};

} // namespace heartstead::simulation
