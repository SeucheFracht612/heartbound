#pragma once

#include "engine/core/result.hpp"
#include "engine/renderer/renderer_stats.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace heartstead::renderer::benchmark {

struct BenchmarkSummary {
    std::string scene;
    std::uint64_t seed = 0;
    std::size_t sample_count = 0;
    std::size_t gpu_sample_count = 0;

    double median_frame_ms = 0.0;
    double p95_frame_ms = 0.0;
    double p99_frame_ms = 0.0;
    double one_percent_low_fps = 0.0;
    double point_one_percent_low_fps = 0.0;
    double maximum_frame_ms = 0.0;

    double mean_cpu_frame_ms = 0.0;
    double mean_gpu_frame_ms = 0.0;
    double mean_meshing_ms = 0.0;
    double mean_upload_ms = 0.0;
    double mean_gpu_wait_ms = 0.0;
    std::uint64_t total_uploaded_bytes = 0;
};

class BenchmarkRecorder {
  public:
    BenchmarkRecorder(std::string scene, std::uint64_t seed);

    void record(RendererStats stats);
    void clear() noexcept;

    [[nodiscard]] const std::vector<RendererStats>& samples() const noexcept;
    [[nodiscard]] BenchmarkSummary summarize() const;
    [[nodiscard]] std::string to_json() const;
    [[nodiscard]] std::string to_csv() const;
    [[nodiscard]] core::Status write_json(const std::filesystem::path& path) const;
    [[nodiscard]] core::Status write_csv(const std::filesystem::path& path) const;

  private:
    std::string scene_;
    std::uint64_t seed_ = 0;
    std::vector<RendererStats> samples_;
};

[[nodiscard]] std::string format_benchmark_summary(const BenchmarkSummary& summary);

} // namespace heartstead::renderer::benchmark
