#include "engine/core/logging.hpp"
#include "engine/modding/generic_prototype.hpp"
#include "engine/modding/prototype_registry.hpp"
#include "engine/world/meshing/chunk_mesher.hpp"
#include "engine/world/regions/region_graph.hpp"
#include "engine/world/streaming/chunk_streamer.hpp"
#include "engine/world/voxels/voxel_palette.hpp"
#include "engine/world/world_state.hpp"
#include "engine/world/worldgen/terrain_generator.hpp"

#include <string>
#include <utility>
#include <vector>

int main() {
    using namespace heartstead;

    const auto clay_id = core::PrototypeId::parse("base:voxels/clay");
    if (!clay_id) {
        core::log(core::LogLevel::error, "sample voxel prototype id is invalid");
        return 1;
    }
    modding::GenericPrototype clay_prototype;
    clay_prototype.kind = std::string(modding::PrototypeKinds::voxel);
    clay_prototype.id = clay_id.value();
    clay_prototype.display_name = "Clay";
    clay_prototype.fields = {
        {"kind", "voxel"},         {"id", clay_id.value().value()},
        {"display_name", "Clay"},  {"terrain_material", "clay"},
        {"mining_tool", "shovel"},
    };

    modding::PrototypeRegistry registry;
    auto registry_build = registry.build({clay_prototype});
    if (registry_build.has_errors()) {
        core::log(core::LogLevel::error, "failed to build sample voxel registry");
        return 1;
    }
    auto palette = world::voxel_palette_from_prototypes(registry);
    if (!palette) {
        core::log(core::LogLevel::error, palette.error().message);
        return 1;
    }
    auto clay_cell = palette.value().cell_for(clay_id.value());
    if (!clay_cell) {
        core::log(core::LogLevel::error, clay_cell.error().message);
        return 1;
    }

    world::RegionDescriptor region;
    region.id = "temperate_valley";
    region.age = "settlement_age";
    region.biome_cluster = "temperate";
    region.resource_rules.push_back({clay_id.value(), "surface_deposit", 1.0});

    world::RegionGraph regions;
    if (auto status = regions.add_region(std::move(region)); !status) {
        core::log(core::LogLevel::error, status.error().message);
        return 1;
    }

    world::TerrainGenerationConfig generation;
    generation.world_seed = 42;
    generation.region_id = "temperate_valley";
    generation.base_surface_y = 8;

    world::WorldState state;
    world::ChunkStreamInterestPolicy stream_policy;
    stream_policy.load_horizontal_radius_chunks = 0;
    stream_policy.load_vertical_radius_chunks = 0;
    stream_policy.retain_horizontal_radius_chunks = 1;
    stream_policy.retain_vertical_radius_chunks = 1;
    const std::vector<simulation::SimulationViewer> viewers{
        {core::NetId::from_value(1), {0, 0, 0}},
    };
    auto maintenance = world::ChunkStreamer::maintain_loaded_interest(
        state, viewers, stream_policy, generation, regions, palette.value());
    if (!maintenance) {
        core::log(core::LogLevel::error, maintenance.error().message);
        return 1;
    }
    if (maintenance.value().loaded_count() != 1) {
        core::log(core::LogLevel::error, "Expected exactly one streamed chunk load");
        return 1;
    }

    (void)state.chunks().get_or_create({1, 0, 0});
    state.chunks().clear_all_dirty();

    auto status = state.chunks().set({0, 0, 0}, {world::VoxelChunk::edge_length - 1, 4, 4},
                                     world::VoxelCell::air());
    if (!status) {
        core::log(core::LogLevel::error, status.error().message);
        return 1;
    }

    const auto stats = state.chunks().stats();
    core::log(core::LogLevel::info, "Chunks: " + std::to_string(stats.chunk_count));
    core::log(core::LogLevel::info, "Voxel edits: " + std::to_string(stats.edit_count));
    core::log(core::LogLevel::info, "Dirty mesh chunks: " + std::to_string(stats.dirty_mesh_count));

    const auto* chunk = state.chunks().find({0, 0, 0});
    if (chunk == nullptr) {
        core::log(core::LogLevel::error, "Generated chunk is missing");
        return 1;
    }
    auto mesh = world::ChunkMesher::build_surface_mesh(*chunk);
    if (!mesh) {
        core::log(core::LogLevel::error, mesh.error().message);
        return 1;
    }
    core::log(core::LogLevel::info, "Mesh faces: " + std::to_string(mesh.value().face_count));
    core::log(core::LogLevel::info,
              "Mesh vertices: " + std::to_string(mesh.value().vertices.size()));

    const auto edited = state.chunks().get({0, 0, 0}, {world::VoxelChunk::edge_length - 1, 4, 4});
    if (!edited || !edited.value().is_air()) {
        core::log(core::LogLevel::error, "Edited voxel did not persist in chunk database");
        return 1;
    }

    return 0;
}
