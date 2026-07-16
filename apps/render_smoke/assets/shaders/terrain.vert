#version 450

// GpuChunkVertex contract (engine/renderer/terrain/gpu_chunk_vertex.hpp).
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;
layout(location = 3) in uint in_voxel_type;
layout(location = 4) in uint in_light;
layout(location = 5) in uint in_state_bits;

layout(location = 0) out vec3 fragment_normal;
layout(location = 1) out vec2 fragment_uv;
layout(location = 2) flat out uint fragment_voxel_type;
layout(location = 3) flat out uint fragment_light;
layout(location = 4) flat out uint fragment_state_bits;

layout(push_constant) uniform ChunkPushConstants {
    mat4 view_projection;
    vec4 camera_relative_origin;
} chunk;

void main() {
    vec3 world_position = in_position + chunk.camera_relative_origin.xyz;
    gl_Position = chunk.view_projection * vec4(world_position, 1.0);
    fragment_normal = in_normal;
    fragment_uv = in_uv;
    fragment_voxel_type = in_voxel_type;
    fragment_light = in_light;
    fragment_state_bits = in_state_bits;
}
