#include "engine/profiling/cpu_timing.hpp"

#include <algorithm>

namespace heartstead::profiling {

namespace {

[[nodiscard]] constexpr std::size_t zone_index(CpuTimingZone zone) noexcept {
    return static_cast<std::size_t>(zone);
}

[[nodiscard]] double elapsed_milliseconds(std::chrono::steady_clock::time_point started) noexcept {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
        .count();
}

} // namespace

void CpuTimingRecorder::reset() noexcept {
    milliseconds_.fill(0.0);
}

void CpuTimingRecorder::add(CpuTimingZone zone, double milliseconds) noexcept {
    if (zone == CpuTimingZone::count) {
        return;
    }
    milliseconds_[zone_index(zone)] += std::max(0.0, milliseconds);
}

double CpuTimingRecorder::milliseconds(CpuTimingZone zone) const noexcept {
    return zone == CpuTimingZone::count ? 0.0 : milliseconds_[zone_index(zone)];
}

ScopedCpuTimingZone::ScopedCpuTimingZone(CpuTimingRecorder& recorder, CpuTimingZone zone) noexcept
    : recorder_(&recorder), zone_(zone), started_(Clock::now()) {}

ScopedCpuTimingZone::~ScopedCpuTimingZone() {
    recorder_->add(zone_, elapsed_milliseconds(started_));
}

ScopedCpuTimer::ScopedCpuTimer(double& destination_milliseconds) noexcept
    : destination_milliseconds_(&destination_milliseconds), started_(Clock::now()) {}

ScopedCpuTimer::~ScopedCpuTimer() {
    *destination_milliseconds_ += elapsed_milliseconds(started_);
}

std::string_view cpu_timing_zone_name(CpuTimingZone zone) noexcept {
    switch (zone) {
    case CpuTimingZone::complete_frame:
        return "complete_frame";
    case CpuTimingZone::render_extraction:
        return "render_extraction";
    case CpuTimingZone::chunk_synchronization:
        return "chunk_synchronization";
    case CpuTimingZone::visibility_culling:
        return "visibility_culling";
    case CpuTimingZone::draw_list_construction:
        return "draw_list_construction";
    case CpuTimingZone::command_build:
        return "command_build";
    case CpuTimingZone::command_recording:
        return "command_recording";
    case CpuTimingZone::chunk_snapshot:
        return "chunk_snapshot";
    case CpuTimingZone::meshing:
        return "meshing";
    case CpuTimingZone::upload_preparation:
        return "upload_preparation";
    case CpuTimingZone::upload:
        return "upload";
    case CpuTimingZone::gpu_synchronization_wait:
        return "gpu_synchronization_wait";
    case CpuTimingZone::count:
        break;
    }
    return "unknown";
}

} // namespace heartstead::profiling
