#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace heartstead::profiling {

enum class CpuTimingZone : std::uint8_t {
    complete_frame,
    render_extraction,
    chunk_synchronization,
    visibility_culling,
    draw_list_construction,
    command_build,
    command_recording,
    chunk_snapshot,
    meshing,
    upload_preparation,
    upload,
    gpu_synchronization_wait,
    count,
};

class CpuTimingRecorder {
  public:
    void reset() noexcept;
    void add(CpuTimingZone zone, double milliseconds) noexcept;
    [[nodiscard]] double milliseconds(CpuTimingZone zone) const noexcept;

  private:
    std::array<double, static_cast<std::size_t>(CpuTimingZone::count)> milliseconds_{};
};

class ScopedCpuTimingZone {
  public:
    ScopedCpuTimingZone(CpuTimingRecorder& recorder, CpuTimingZone zone) noexcept;
    ~ScopedCpuTimingZone();

    ScopedCpuTimingZone(const ScopedCpuTimingZone&) = delete;
    ScopedCpuTimingZone& operator=(const ScopedCpuTimingZone&) = delete;

  private:
    using Clock = std::chrono::steady_clock;

    CpuTimingRecorder* recorder_ = nullptr;
    CpuTimingZone zone_ = CpuTimingZone::complete_frame;
    Clock::time_point started_{};
};

class ScopedCpuTimer {
  public:
    explicit ScopedCpuTimer(double& destination_milliseconds) noexcept;
    ~ScopedCpuTimer();

    ScopedCpuTimer(const ScopedCpuTimer&) = delete;
    ScopedCpuTimer& operator=(const ScopedCpuTimer&) = delete;

  private:
    using Clock = std::chrono::steady_clock;

    double* destination_milliseconds_ = nullptr;
    Clock::time_point started_{};
};

[[nodiscard]] std::string_view cpu_timing_zone_name(CpuTimingZone zone) noexcept;

} // namespace heartstead::profiling
