#include "engine/renderer/renderer_stats.hpp"

#include <iomanip>
#include <sstream>

namespace heartstead::renderer {

std::string format_renderer_stats(const RendererStats& stats) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(3) << "frame=" << stats.frame_index
           << " submit=" << stats.submission_serial << '/' << stats.completed_submission_serial
           << " cpu=" << stats.cpu_frame_ms << "ms";
    if (stats.gpu_timing_valid) {
        stream << " gpu=" << stats.gpu_frame_ms << "ms"
               << " gpu_frame=" << stats.gpu_timing_frame_index
               << " phases=" << stats.gpu_opaque_terrain_ms << '/'
               << stats.gpu_alpha_tested_terrain_ms << '/'
               << stats.gpu_transparent_terrain_ms << "ms";
    } else {
        stream << " gpu=pending";
    }
    if (stats.gpu_upload_timing_valid) {
        stream << " gpu_upload=" << stats.gpu_upload_ms << "ms"
               << " upload_submit=" << stats.gpu_upload_submission_serial;
    }
    stream << " sync=" << stats.chunk_synchronization_ms << "ms" << " mesh=" << stats.meshing_ms
           << "ms" << " upload=" << stats.upload_ms << "ms" << " cull=" << stats.culling_ms << "ms"
           << " record=" << stats.command_recording_ms << "ms" << " wait=" << stats.gpu_wait_ms
           << "ms" << " chunks=" << stats.visible_chunks << '/' << stats.resident_chunks << '/'
           << stats.loaded_chunks << " draws=" << stats.draw_calls << '['
           << stats.opaque_terrain_draws << '/' << stats.alpha_tested_terrain_draws << '/'
           << stats.transparent_terrain_draws << ']'
           << " pipelines=" << stats.pipeline_switches << " textures=" << stats.resident_textures
           << '/' << stats.resident_texture_bytes << " materials=" << stats.runtime_materials
           << " triangles=" << stats.triangles << " resident_bytes=" << stats.resident_mesh_bytes
           << '/' << stats.gpu_terrain_budget_bytes
           << " suppressed=" << stats.residency_suppressed_chunks
           << " evicted=" << stats.distance_evicted_meshes << '/'
           << stats.memory_pressure_evicted_meshes << " arena=" << stats.gpu_arena_used_bytes << '/'
           << stats.gpu_arena_capacity_bytes << " arena_frag=" << stats.gpu_arena_fragmentation;
    return stream.str();
}

} // namespace heartstead::renderer
