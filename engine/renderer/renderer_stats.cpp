#include "engine/renderer/renderer_stats.hpp"

#include <iomanip>
#include <sstream>

namespace heartstead::renderer {

std::string format_renderer_stats(const RendererStats& stats) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(3) << "frame=" << stats.frame_index
           << " cpu=" << stats.cpu_frame_ms << "ms";
    if (stats.gpu_timing_valid) {
        stream << " gpu=" << stats.gpu_frame_ms << "ms"
               << " gpu_frame=" << stats.gpu_timing_frame_index;
    } else {
        stream << " gpu=pending";
    }
    stream << " sync=" << stats.chunk_synchronization_ms << "ms" << " mesh=" << stats.meshing_ms
           << "ms" << " upload=" << stats.upload_ms << "ms" << " cull=" << stats.culling_ms << "ms"
           << " record=" << stats.command_recording_ms << "ms" << " wait=" << stats.gpu_wait_ms
           << "ms" << " chunks=" << stats.visible_chunks << '/' << stats.resident_chunks << '/'
           << stats.loaded_chunks << " draws=" << stats.draw_calls
           << " triangles=" << stats.triangles << " resident_bytes=" << stats.resident_mesh_bytes
           << " arena=" << stats.gpu_arena_used_bytes << '/' << stats.gpu_arena_capacity_bytes
           << " arena_frag=" << stats.gpu_arena_fragmentation;
    return stream.str();
}

} // namespace heartstead::renderer
