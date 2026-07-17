#include "engine/simulation/fixed_step.hpp"

#include <algorithm>
#include <limits>

namespace heartstead::simulation {

core::Status FixedStepConfig::validate() const {
    if (ticks_per_second == 0 || ticks_per_second > 1000) {
        return core::Status::failure("fixed_step.invalid_rate",
                                     "fixed-step rate must be between 1 and 1000 Hz");
    }
    if (maximum_steps_per_frame == 0 || maximum_steps_per_frame > 1024) {
        return core::Status::failure("fixed_step.invalid_catch_up",
                                     "fixed-step catch-up limit must be between 1 and 1024");
    }
    if (maximum_frame_time_us == 0 || maximum_frame_time_us > 10'000'000) {
        return core::Status::failure("fixed_step.invalid_frame_limit",
                                     "fixed-step frame limit must be between 1 us and 10 seconds");
    }
    return core::Status::ok();
}

FixedStepClock::FixedStepClock(FixedStepConfig config) : config_(config) {}

core::Result<FixedStepFrame> FixedStepClock::advance(std::uint64_t frame_time_us) {
    auto status = config_.validate();
    if (!status) {
        return core::Result<FixedStepFrame>::failure(status.error().code, status.error().message);
    }

    FixedStepFrame frame;
    frame.first_tick = tick_ + 1;
    frame.step_us = step_us();
    const auto accepted = std::min(frame_time_us, config_.maximum_frame_time_us);
    frame.dropped_time_us = frame_time_us - accepted;

    if (accepted > std::numeric_limits<std::uint64_t>::max() / config_.ticks_per_second) {
        return core::Result<FixedStepFrame>::failure("fixed_step.frame_overflow",
                                                     "fixed-step frame duration overflows");
    }
    accumulator_scaled_ += accepted * config_.ticks_per_second;
    constexpr std::uint64_t one_second_us = 1'000'000;
    const auto available_steps = accumulator_scaled_ / one_second_us;
    frame.step_count = static_cast<std::uint32_t>(
        std::min<std::uint64_t>(available_steps, config_.maximum_steps_per_frame));
    accumulator_scaled_ -= static_cast<std::uint64_t>(frame.step_count) * one_second_us;
    tick_ += frame.step_count;

    if (available_steps > config_.maximum_steps_per_frame) {
        const auto excess_steps = available_steps - config_.maximum_steps_per_frame;
        accumulator_scaled_ -= excess_steps * one_second_us;
        frame.dropped_time_us += (excess_steps * one_second_us) / config_.ticks_per_second;
    }
    frame.interpolation_alpha =
        static_cast<double>(accumulator_scaled_) / static_cast<double>(one_second_us);
    return core::Result<FixedStepFrame>::success(frame);
}

void FixedStepClock::reset(std::uint64_t tick) noexcept {
    accumulator_scaled_ = 0;
    tick_ = tick;
}

const FixedStepConfig& FixedStepClock::config() const noexcept {
    return config_;
}

std::uint64_t FixedStepClock::tick() const noexcept {
    return tick_;
}

std::uint64_t FixedStepClock::step_us() const noexcept {
    return 1'000'000 / config_.ticks_per_second;
}

} // namespace heartstead::simulation
