#pragma once

#include <cstdint>
#include <string>

namespace heartstead::renderer {

struct RendererStats {
    std::uint64_t frame_index = 0;
    std::uint64_t gpu_timing_frame_index = 0;
    bool gpu_timing_valid = false;
    std::uint32_t gpu_timing_latency_frames = 0;

    double cpu_frame_ms = 0.0;
    double gpu_frame_ms = 0.0;
    double render_extraction_ms = 0.0;
    double chunk_synchronization_ms = 0.0;
    double culling_ms = 0.0;
    double draw_list_ms = 0.0;
    double command_build_ms = 0.0;
    double command_recording_ms = 0.0;
    double chunk_snapshot_ms = 0.0;
    double meshing_ms = 0.0;
    double upload_preparation_ms = 0.0;
    double upload_ms = 0.0;
    double gpu_wait_ms = 0.0;

    double gpu_opaque_terrain_ms = 0.0;
    double gpu_transfer_ms = 0.0;
    double gpu_final_copy_ms = 0.0;

    std::uint32_t loaded_chunks = 0;
    std::uint32_t mesh_pending_chunks = 0;
    std::uint32_t upload_pending_chunks = 0;
    std::uint32_t resident_chunks = 0;
    std::uint32_t visible_chunks = 0;
    std::uint32_t culled_chunks = 0;
    std::uint32_t drawn_chunks = 0;
    std::uint32_t draw_calls = 0;

    std::uint64_t vertices = 0;
    std::uint64_t triangles = 0;
    std::uint64_t resident_mesh_bytes = 0;
    std::uint64_t pending_upload_bytes = 0;
    std::uint64_t uploaded_bytes_this_frame = 0;
};

[[nodiscard]] std::string format_renderer_stats(const RendererStats& stats);

} // namespace heartstead::renderer
