#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;

struct GpuObjectInstance {
    mat4 camera_relative_transform;
    vec4 color;
    uvec4 metadata;
};

layout(std430, set = 0, binding = 0) readonly buffer ObjectInstances {
    GpuObjectInstance instances[];
} object_instances;

layout(push_constant) uniform FramePushConstants {
    mat4 view_projection;
    vec4 unused_origin;
    vec4 sun_direction_intensity;
    vec4 ambient_color_fog_start;
    vec4 fog_color_fog_end;
} frame;

layout(location = 0) out vec3 fragment_normal;
layout(location = 1) out vec3 fragment_world_position;
layout(location = 2) out vec2 fragment_uv;
layout(location = 3) out vec4 fragment_color;
layout(location = 4) flat out uint fragment_layer;

void main() {
    GpuObjectInstance instance = object_instances.instances[gl_InstanceIndex];
    vec4 world_position = instance.camera_relative_transform * vec4(in_position, 1.0);
    gl_Position = frame.view_projection * world_position;
    fragment_normal = normalize(transpose(inverse(mat3(instance.camera_relative_transform))) *
                                in_normal);
    fragment_world_position = world_position.xyz;
    fragment_uv = in_uv;
    fragment_color = instance.color;
    fragment_layer = instance.metadata.x;
}
