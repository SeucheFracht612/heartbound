#version 450

layout(location = 0) in vec3 fragment_normal;
layout(location = 1) in vec2 fragment_uv;
layout(location = 2) flat in uint fragment_voxel_type;
layout(location = 3) flat in uint fragment_light;
layout(location = 4) flat in uint fragment_state_bits;

layout(location = 0) out vec4 out_color;

vec3 voxel_palette(uint voxel_type) {
    const vec3 colors[6] = vec3[6](
        vec3(0.32, 0.60, 0.25),
        vec3(0.47, 0.31, 0.18),
        vec3(0.43, 0.45, 0.48),
        vec3(0.72, 0.64, 0.39),
        vec3(0.22, 0.44, 0.19),
        vec3(0.48, 0.35, 0.24));
    return colors[int((voxel_type - 1U) % 6U)];
}

void main() {
    vec3 normal = normalize(fragment_normal);
    vec3 light_direction = normalize(vec3(0.45, 0.82, 0.35));
    float directional = 0.32 + 0.68 * max(dot(normal, light_direction), 0.0);
    float baked_light = 0.55 + 0.45 * (float(fragment_light) / 255.0);
    float checker = mod(floor(fragment_uv.x * 4.0) + floor(fragment_uv.y * 4.0), 2.0);
    float surface_detail = mix(0.96, 1.04, checker);
    float state_tint = (fragment_state_bits & 1U) != 0U ? 0.94 : 1.0;
    vec3 color = voxel_palette(fragment_voxel_type) * directional * baked_light *
                 surface_detail * state_tint;
    out_color = vec4(color, 1.0);
}
