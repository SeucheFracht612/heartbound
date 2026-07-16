#version 450

layout(location = 0) in vec3 fragment_normal;
layout(location = 1) in vec2 fragment_uv;
layout(location = 2) flat in uint fragment_voxel_type;
layout(location = 3) flat in uint fragment_light;
layout(location = 4) flat in uint fragment_state_bits;

layout(set = 0, binding = 0) uniform sampler2DArray terrain_textures;

struct GpuVoxelMaterial {
    uvec4 texture_layers_and_flags;
    vec4 base_color;
    vec4 surface_parameters;
};

layout(std430, set = 0, binding = 1) readonly buffer VoxelMaterialTable {
    GpuVoxelMaterial materials[];
} voxel_material_table;

layout(location = 0) out vec4 out_color;

void main() {
    uint table_length = uint(voxel_material_table.materials.length());
    uint material_index = min(fragment_voxel_type, max(table_length, 1U) - 1U);
    GpuVoxelMaterial material = voxel_material_table.materials[material_index];

    vec3 normal = normalize(fragment_normal);
    uint texture_layer = material.texture_layers_and_flags.x;
    if (normal.y > 0.5) {
        texture_layer = material.texture_layers_and_flags.y;
    } else if (normal.y < -0.5) {
        texture_layer = material.texture_layers_and_flags.z;
    }
    vec4 texel = texture(terrain_textures, vec3(fragment_uv, float(texture_layer)));

    vec3 light_direction = normalize(vec3(0.45, 0.82, 0.35));
    float directional = 0.32 + 0.68 * max(dot(normal, light_direction), 0.0);
    float baked_light = 0.55 + 0.45 * (float(fragment_light) / 255.0);
    float state_tint = (fragment_state_bits & 1U) != 0U ? 0.94 : 1.0;
    float emissive = material.surface_parameters.x;
    vec3 lit = texel.rgb * material.base_color.rgb * directional * baked_light * state_tint;
    vec3 color = mix(lit, texel.rgb * material.base_color.rgb, clamp(emissive, 0.0, 1.0));
    out_color = vec4(color, texel.a * material.base_color.a);
}
