#include "engine/simulation/world_time.hpp"

#include <limits>
#include <string>

namespace heartstead::simulation {

namespace {

[[nodiscard]] core::Result<WorldTick> checked_multiply(WorldTick left, WorldTick right,
                                                       const char* label) {
    if (left != 0 && right > std::numeric_limits<WorldTick>::max() / left) {
        return core::Result<WorldTick>::failure(
            "world_time.config_overflow", std::string("world time ") + label + " overflows u64");
    }
    return core::Result<WorldTick>::success(left * right);
}

} // namespace

core::Status WorldTimeConfig::validate() const {
    if (ticks_per_second == 0 || seconds_per_minute == 0 || minutes_per_hour == 0 ||
        hours_per_day == 0) {
        return core::Status::failure("world_time.invalid_config",
                                     "world time conversion factors must be non-zero");
    }
    auto day = ticks_per_day();
    if (!day) {
        return core::Status::failure(day.error().code, day.error().message);
    }
    return core::Status::ok();
}

core::Result<WorldTick> WorldTimeConfig::ticks_per_hour() const {
    if (ticks_per_second == 0 || seconds_per_minute == 0 || minutes_per_hour == 0) {
        return core::Result<WorldTick>::failure("world_time.invalid_config",
                                                "world time conversion factors must be non-zero");
    }
    auto per_minute = checked_multiply(ticks_per_second, seconds_per_minute, "minute");
    if (!per_minute) {
        return per_minute;
    }
    return checked_multiply(per_minute.value(), minutes_per_hour, "hour");
}

core::Result<WorldTick> WorldTimeConfig::ticks_per_day() const {
    auto per_hour = ticks_per_hour();
    if (!per_hour) {
        return per_hour;
    }
    if (hours_per_day == 0) {
        return core::Result<WorldTick>::failure("world_time.invalid_config",
                                                "world time conversion factors must be non-zero");
    }
    return checked_multiply(per_hour.value(), hours_per_day, "day");
}

core::Status WorldClock::advance(WorldTick delta) noexcept {
    if (delta > std::numeric_limits<WorldTick>::max() - now_) {
        return core::Status::failure("world_time.overflow", "world time cannot overflow u64");
    }
    now_ += delta;
    return core::Status::ok();
}

core::Status WorldClock::advance_hours(std::uint64_t hours,
                                       const WorldTimeConfig& config) noexcept {
    auto per_hour = config.ticks_per_hour();
    if (!per_hour) {
        return core::Status::failure(per_hour.error().code, per_hour.error().message);
    }
    auto delta = checked_multiply(per_hour.value(), hours, "hour advance");
    if (!delta) {
        return core::Status::failure(delta.error().code, delta.error().message);
    }
    return advance(delta.value());
}

} // namespace heartstead::simulation
