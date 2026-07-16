#include "engine/renderer/benchmark/benchmark_statistics.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <sstream>

namespace heartstead::renderer::benchmark {

namespace {

[[nodiscard]] double percentile(const std::vector<double>& sorted, double fraction) noexcept {
    if (sorted.empty()) {
        return 0.0;
    }
    const auto position = fraction * static_cast<double>(sorted.size() - 1);
    const auto lower = static_cast<std::size_t>(std::floor(position));
    const auto upper = static_cast<std::size_t>(std::ceil(position));
    const auto weight = position - static_cast<double>(lower);
    return sorted[lower] * (1.0 - weight) + sorted[upper] * weight;
}

[[nodiscard]] double low_fps(const std::vector<double>& sorted, double slow_fraction) noexcept {
    if (sorted.empty()) {
        return 0.0;
    }
    const auto count = std::max<std::size_t>(
        1, static_cast<std::size_t>(std::ceil(static_cast<double>(sorted.size()) * slow_fraction)));
    const auto start = sorted.size() - count;
    const auto total =
        std::accumulate(sorted.begin() + static_cast<std::ptrdiff_t>(start), sorted.end(), 0.0);
    const auto mean_slow_frame_ms = total / static_cast<double>(count);
    return mean_slow_frame_ms > 0.0 ? 1'000.0 / mean_slow_frame_ms : 0.0;
}

[[nodiscard]] std::string json_escape(std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const char character : value) {
        switch (character) {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped += character;
            break;
        }
    }
    return escaped;
}

[[nodiscard]] core::Status write_text_file(const std::filesystem::path& path,
                                           std::string_view text) {
    std::error_code error;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) {
            return core::Status::failure("renderer.benchmark_create_directory_failed",
                                         "failed to create benchmark output directory: " +
                                             error.message());
        }
    }
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        return core::Status::failure("renderer.benchmark_open_output_failed",
                                     "failed to open benchmark output file: " + path.string());
    }
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!stream) {
        return core::Status::failure("renderer.benchmark_write_output_failed",
                                     "failed to write benchmark output file: " + path.string());
    }
    return core::Status::ok();
}

} // namespace

BenchmarkRecorder::BenchmarkRecorder(std::string scene, std::uint64_t seed)
    : scene_(std::move(scene)), seed_(seed) {}

void BenchmarkRecorder::record(RendererStats stats) {
    samples_.push_back(stats);
}

void BenchmarkRecorder::clear() noexcept {
    samples_.clear();
}

const std::vector<RendererStats>& BenchmarkRecorder::samples() const noexcept {
    return samples_;
}

BenchmarkSummary BenchmarkRecorder::summarize() const {
    BenchmarkSummary summary;
    summary.scene = scene_;
    summary.seed = seed_;
    summary.sample_count = samples_.size();
    if (samples_.empty()) {
        return summary;
    }

    std::vector<double> frame_times;
    frame_times.reserve(samples_.size());
    double cpu_total = 0.0;
    double gpu_total = 0.0;
    double gpu_upload_total = 0.0;
    double meshing_total = 0.0;
    double upload_total = 0.0;
    double gpu_wait_total = 0.0;
    for (const auto& sample : samples_) {
        frame_times.push_back(sample.cpu_frame_ms);
        cpu_total += sample.cpu_frame_ms;
        meshing_total += sample.meshing_ms;
        upload_total += sample.upload_ms;
        gpu_wait_total += sample.gpu_wait_ms;
        summary.total_uploaded_bytes += sample.uploaded_bytes_this_frame;
        if (sample.gpu_timing_valid) {
            gpu_total += sample.gpu_frame_ms;
            ++summary.gpu_sample_count;
        }
        if (sample.gpu_upload_timing_valid) {
            gpu_upload_total += sample.gpu_upload_ms;
            ++summary.gpu_upload_sample_count;
        }
    }
    std::ranges::sort(frame_times);
    summary.median_frame_ms = percentile(frame_times, 0.50);
    summary.p95_frame_ms = percentile(frame_times, 0.95);
    summary.p99_frame_ms = percentile(frame_times, 0.99);
    summary.one_percent_low_fps = low_fps(frame_times, 0.01);
    summary.point_one_percent_low_fps = low_fps(frame_times, 0.001);
    summary.maximum_frame_ms = frame_times.back();
    const auto sample_count = static_cast<double>(samples_.size());
    summary.mean_cpu_frame_ms = cpu_total / sample_count;
    summary.mean_meshing_ms = meshing_total / sample_count;
    summary.mean_upload_ms = upload_total / sample_count;
    summary.mean_gpu_wait_ms = gpu_wait_total / sample_count;
    if (summary.gpu_sample_count != 0) {
        summary.mean_gpu_frame_ms = gpu_total / static_cast<double>(summary.gpu_sample_count);
    }
    if (summary.gpu_upload_sample_count != 0) {
        summary.mean_gpu_upload_ms =
            gpu_upload_total / static_cast<double>(summary.gpu_upload_sample_count);
    }
    return summary;
}

std::string BenchmarkRecorder::to_json() const {
    const auto summary = summarize();
    std::ostringstream output;
    output << std::fixed << std::setprecision(6);
    output << "{\n  \"scene\": \"" << json_escape(scene_) << "\",\n"
           << "  \"seed\": " << seed_ << ",\n"
           << "  \"summary\": {\n"
           << "    \"sample_count\": " << summary.sample_count << ",\n"
           << "    \"gpu_sample_count\": " << summary.gpu_sample_count << ",\n"
           << "    \"gpu_upload_sample_count\": " << summary.gpu_upload_sample_count << ",\n"
           << "    \"median_frame_ms\": " << summary.median_frame_ms << ",\n"
           << "    \"p95_frame_ms\": " << summary.p95_frame_ms << ",\n"
           << "    \"p99_frame_ms\": " << summary.p99_frame_ms << ",\n"
           << "    \"one_percent_low_fps\": " << summary.one_percent_low_fps << ",\n"
           << "    \"point_one_percent_low_fps\": " << summary.point_one_percent_low_fps << ",\n"
           << "    \"maximum_frame_ms\": " << summary.maximum_frame_ms << ",\n"
           << "    \"mean_cpu_frame_ms\": " << summary.mean_cpu_frame_ms << ",\n"
           << "    \"mean_gpu_frame_ms\": " << summary.mean_gpu_frame_ms << ",\n"
           << "    \"mean_gpu_upload_ms\": " << summary.mean_gpu_upload_ms << ",\n"
           << "    \"mean_meshing_ms\": " << summary.mean_meshing_ms << ",\n"
           << "    \"mean_upload_ms\": " << summary.mean_upload_ms << ",\n"
           << "    \"mean_gpu_wait_ms\": " << summary.mean_gpu_wait_ms << ",\n"
           << "    \"total_uploaded_bytes\": " << summary.total_uploaded_bytes << "\n"
           << "  },\n  \"frames\": [\n";
    for (std::size_t index = 0; index < samples_.size(); ++index) {
        const auto& sample = samples_[index];
        output << "    {\"frame\": " << sample.frame_index
               << ", \"submission_serial\": " << sample.submission_serial
               << ", \"completed_submission_serial\": " << sample.completed_submission_serial
               << ", \"cpu_frame_ms\": " << sample.cpu_frame_ms
               << ", \"gpu_valid\": " << (sample.gpu_timing_valid ? "true" : "false")
               << ", \"gpu_timing_frame\": " << sample.gpu_timing_frame_index
               << ", \"gpu_latency_frames\": " << sample.gpu_timing_latency_frames
               << ", \"gpu_frame_ms\": " << sample.gpu_frame_ms
               << ", \"gpu_upload_valid\": " << (sample.gpu_upload_timing_valid ? "true" : "false")
               << ", \"gpu_upload_submission_serial\": " << sample.gpu_upload_submission_serial
               << ", \"gpu_upload_ms\": " << sample.gpu_upload_ms
               << ", \"gpu_opaque_ms\": " << sample.gpu_opaque_terrain_ms
               << ", \"gpu_transfer_ms\": " << sample.gpu_transfer_ms
               << ", \"gpu_final_copy_ms\": " << sample.gpu_final_copy_ms
               << ", \"extraction_ms\": " << sample.render_extraction_ms
               << ", \"sync_ms\": " << sample.chunk_synchronization_ms
               << ", \"culling_ms\": " << sample.culling_ms
               << ", \"draw_list_ms\": " << sample.draw_list_ms
               << ", \"command_build_ms\": " << sample.command_build_ms
               << ", \"command_recording_ms\": " << sample.command_recording_ms
               << ", \"snapshot_ms\": " << sample.chunk_snapshot_ms
               << ", \"meshing_ms\": " << sample.meshing_ms
               << ", \"upload_preparation_ms\": " << sample.upload_preparation_ms
               << ", \"upload_ms\": " << sample.upload_ms
               << ", \"gpu_wait_ms\": " << sample.gpu_wait_ms
               << ", \"loaded_chunks\": " << sample.loaded_chunks
               << ", \"mesh_pending_chunks\": " << sample.mesh_pending_chunks
               << ", \"upload_pending_chunks\": " << sample.upload_pending_chunks
               << ", \"resident_chunks\": " << sample.resident_chunks
               << ", \"visible_chunks\": " << sample.visible_chunks
               << ", \"culled_chunks\": " << sample.culled_chunks
               << ", \"drawn_chunks\": " << sample.drawn_chunks
               << ", \"draw_calls\": " << sample.draw_calls
               << ", \"pipeline_switches\": " << sample.pipeline_switches
               << ", \"resident_textures\": " << sample.resident_textures
               << ", \"runtime_materials\": " << sample.runtime_materials
               << ", \"resident_pipelines\": " << sample.resident_pipelines
               << ", \"vertices\": " << sample.vertices << ", \"triangles\": " << sample.triangles
               << ", \"resident_texture_bytes\": " << sample.resident_texture_bytes
               << ", \"resident_mesh_bytes\": " << sample.resident_mesh_bytes
               << ", \"gpu_arena_capacity_bytes\": " << sample.gpu_arena_capacity_bytes
               << ", \"gpu_arena_used_bytes\": " << sample.gpu_arena_used_bytes
               << ", \"gpu_arena_free_bytes\": " << sample.gpu_arena_free_bytes
               << ", \"gpu_arena_fragmentation\": " << sample.gpu_arena_fragmentation
               << ", \"pending_upload_bytes\": " << sample.pending_upload_bytes
               << ", \"uploaded_bytes\": " << sample.uploaded_bytes_this_frame << "}";
        output << (index + 1 == samples_.size() ? "\n" : ",\n");
    }
    output << "  ]\n}\n";
    return output.str();
}

std::string BenchmarkRecorder::to_csv() const {
    std::ostringstream output;
    output << std::fixed << std::setprecision(6);
    output << "scene,seed,frame,submission_serial,completed_submission_serial,cpu_frame_ms,"
              "gpu_valid,gpu_timing_frame,gpu_latency_frames,gpu_frame_ms,gpu_upload_valid,"
              "gpu_upload_submission_serial,gpu_upload_ms,gpu_opaque_ms,gpu_transfer_ms,"
              "gpu_final_copy_ms,extraction_ms,"
              "sync_ms,culling_ms,draw_list_ms,command_build_ms,command_recording_ms,snapshot_ms,"
              "meshing_ms,upload_preparation_ms,upload_ms,gpu_wait_ms,loaded_chunks,"
              "mesh_pending_chunks,upload_pending_chunks,resident_chunks,visible_chunks,"
              "culled_chunks,drawn_chunks,draw_calls,pipeline_switches,resident_textures,"
              "runtime_materials,resident_pipelines,vertices,triangles,resident_texture_bytes,"
              "resident_mesh_bytes,"
              "gpu_arena_capacity_bytes,gpu_arena_used_bytes,gpu_arena_free_bytes,"
              "gpu_arena_fragmentation,pending_upload_bytes,uploaded_bytes\n";
    for (const auto& sample : samples_) {
        output << '"' << scene_ << "\"," << seed_ << ',' << sample.frame_index << ','
               << sample.submission_serial << ',' << sample.completed_submission_serial << ','
               << sample.cpu_frame_ms << ',' << (sample.gpu_timing_valid ? 1 : 0) << ','
               << sample.gpu_timing_frame_index << ',' << sample.gpu_timing_latency_frames << ','
               << sample.gpu_frame_ms << ',' << (sample.gpu_upload_timing_valid ? 1 : 0) << ','
               << sample.gpu_upload_submission_serial << ',' << sample.gpu_upload_ms << ','
               << sample.gpu_opaque_terrain_ms << ',' << sample.gpu_transfer_ms << ','
               << sample.gpu_final_copy_ms << ',' << sample.render_extraction_ms << ','
               << sample.chunk_synchronization_ms << ',' << sample.culling_ms << ','
               << sample.draw_list_ms << ',' << sample.command_build_ms << ','
               << sample.command_recording_ms << ',' << sample.chunk_snapshot_ms << ','
               << sample.meshing_ms << ',' << sample.upload_preparation_ms << ','
               << sample.upload_ms << ',' << sample.gpu_wait_ms << ',' << sample.loaded_chunks
               << ',' << sample.mesh_pending_chunks << ',' << sample.upload_pending_chunks << ','
               << sample.resident_chunks << ',' << sample.visible_chunks << ','
               << sample.culled_chunks << ',' << sample.drawn_chunks << ',' << sample.draw_calls
               << ',' << sample.pipeline_switches << ',' << sample.resident_textures << ','
               << sample.runtime_materials << ',' << sample.resident_pipelines << ','
               << sample.vertices << ',' << sample.triangles << ',' << sample.resident_texture_bytes
               << ',' << sample.resident_mesh_bytes << ',' << sample.gpu_arena_capacity_bytes << ','
               << sample.gpu_arena_used_bytes << ',' << sample.gpu_arena_free_bytes << ','
               << sample.gpu_arena_fragmentation << ',' << sample.pending_upload_bytes << ','
               << sample.uploaded_bytes_this_frame << '\n';
    }
    return output.str();
}

core::Status BenchmarkRecorder::write_json(const std::filesystem::path& path) const {
    return write_text_file(path, to_json());
}

core::Status BenchmarkRecorder::write_csv(const std::filesystem::path& path) const {
    return write_text_file(path, to_csv());
}

std::string format_benchmark_summary(const BenchmarkSummary& summary) {
    std::ostringstream output;
    output << std::fixed << std::setprecision(3) << summary.scene << ": n=" << summary.sample_count
           << " median=" << summary.median_frame_ms << "ms p95=" << summary.p95_frame_ms
           << "ms p99=" << summary.p99_frame_ms << "ms 1%low=" << summary.one_percent_low_fps
           << "fps 0.1%low=" << summary.point_one_percent_low_fps
           << "fps max=" << summary.maximum_frame_ms << "ms cpu=" << summary.mean_cpu_frame_ms
           << "ms gpu=";
    if (summary.gpu_sample_count == 0) {
        output << "unavailable";
    } else {
        output << summary.mean_gpu_frame_ms << "ms";
    }
    output << " gpu_upload=";
    if (summary.gpu_upload_sample_count == 0) {
        output << "unavailable";
    } else {
        output << summary.mean_gpu_upload_ms << "ms";
    }
    output << " mesh=" << summary.mean_meshing_ms << "ms upload=" << summary.mean_upload_ms
           << "ms wait=" << summary.mean_gpu_wait_ms << "ms";
    return output.str();
}

} // namespace heartstead::renderer::benchmark
