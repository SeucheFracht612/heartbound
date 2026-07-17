#pragma once

#include "engine/core/result.hpp"

#include <cstdint>

namespace heartstead::simulation {

struct FixedStepConfig {
    std::uint32_t ticks_per_second = 60;
    std::uint32_t maximum_steps_per_frame = 8;
    std::uint64_t maximum_frame_time_us = 250'000;

    [[nodiscard]] core::Status validate() const;
};

struct FixedStepFrame {
    std::uint32_t step_count = 0;
    std::uint64_t first_tick = 0;
    std::uint64_t step_us = 0;
    double interpolation_alpha = 0.0;
    std::uint64_t dropped_time_us = 0;
};

class FixedStepClock {
  public:
    explicit FixedStepClock(FixedStepConfig config = {});

    [[nodiscard]] core::Result<FixedStepFrame> advance(std::uint64_t frame_time_us);
    void reset(std::uint64_t tick = 0) noexcept;

    [[nodiscard]] const FixedStepConfig& config() const noexcept;
    [[nodiscard]] std::uint64_t tick() const noexcept;
    [[nodiscard]] std::uint64_t step_us() const noexcept;

  private:
    FixedStepConfig config_;
    std::uint64_t accumulator_scaled_ = 0;
    std::uint64_t tick_ = 0;
};

} // namespace heartstead::simulation
