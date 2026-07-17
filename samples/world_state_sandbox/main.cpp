#include "engine/core/logging.hpp"
#include "engine/core/process_entry.hpp"
#include "engine/processes/process.hpp"
#include "engine/world/world_snapshot.hpp"
#include "engine/world/world_state.hpp"

#include <string>
#include <utility>

int main(int argc, char** argv) {
    return heartstead::core::run_no_argument_process_entry(argc, argv, [] {
        using namespace heartstead;

        world::WorldStateDesc desc;
        desc.metadata.game_version = "world_state_sandbox";
        desc.metadata.world_seed = 1234;
        world::WorldState state(desc);

        auto build_id = state.save_ids().reserve();
        if (!build_id) {
            core::log(core::LogLevel::error, "failed to reserve save ids");
            return 1;
        }

        auto wall_prototype = core::PrototypeId::parse("base:build_pieces/test_wall");
        auto process_prototype = core::PrototypeId::parse("base:processes/drying");
        if (!wall_prototype || !process_prototype) {
            core::log(core::LogLevel::error, "failed to parse prototype ids");
            return 1;
        }

        auto voxel_status =
            state.chunks().set({0, 0, 0}, {0, 0, 0}, world::VoxelCell{2, 0}, state.dirty_regions());
        if (!voxel_status) {
            core::log(core::LogLevel::error, voxel_status.error().message);
            return 1;
        }

        auto clay_voxel = core::PrototypeId::parse("base:voxels/clay");
        if (!clay_voxel) {
            core::log(core::LogLevel::error, "failed to parse region resource prototype id");
            return 1;
        }
        world::RegionDescriptor region;
        region.id = "temperate_valley";
        region.age = "settlement_age";
        region.biome_cluster = "temperate";
        region.sub_biomes = {"meadow", "clay_banks"};
        region.resource_rules.push_back({clay_voxel.value(), "surface_deposit", 0.5});
        region.ecology_parameters.emplace("soil_fertility", 0.8);
        if (auto status = state.regions().add_region(std::move(region)); !status) {
            core::log(core::LogLevel::error, status.error().message);
            return 1;
        }

        build::BuildPieceRecord wall;
        wall.object_id = build_id.value();
        wall.prototype_id = wall_prototype.value();
        wall.room_contribution_tags.push_back("wall");
        if (auto status = state.build_objects().insert(std::move(wall)); !status) {
            core::log(core::LogLevel::error, status.error().message);
            return 1;
        }

        auto process = processes::ProcessRuntime::create(
            core::ProcessId::from_value(1), build_id.value(), process_prototype.value(), 0, 1000);
        if (!process) {
            core::log(core::LogLevel::error, process.error().message);
            return 1;
        }
        auto process_status = state.processes().insert(std::move(process).value());
        if (!process_status) {
            core::log(core::LogLevel::error, process_status.error().message);
            return 1;
        }
        auto advanced = state.processes().advance_all(1000, processes::ProcessModifiers{});
        if (!advanced) {
            core::log(core::LogLevel::error, advanced.error().message);
            return 1;
        }

        auto& road = state.networks().get_or_create(networks::NetworkKind::road);
        auto road_status = road.add_node(
            networks::NetworkNode{networks::NetworkNodeId::from_value(1), {0, 0, 0}, 1, "gate"});
        if (!road_status) {
            core::log(core::LogLevel::error, road_status.error().message);
            return 1;
        }

        const auto stats = state.stats();
        core::log(core::LogLevel::info, "World chunks: " + std::to_string(stats.chunk_count));
        core::log(core::LogLevel::info, "Regions: " + std::to_string(stats.region_count));
        core::log(core::LogLevel::info,
                  "Build objects: " + std::to_string(stats.build_object_count));
        core::log(core::LogLevel::info, "Processes: " + std::to_string(stats.process_count));
        core::log(core::LogLevel::info, "Networks: " + std::to_string(stats.network_count));

        auto snapshot = world::WorldSnapshotBridge::export_snapshot(state);
        if (!snapshot) {
            core::log(core::LogLevel::error, snapshot.error().message);
            return 1;
        }

        auto restored = world::WorldSnapshotBridge::import_snapshot(snapshot.value());
        if (!restored) {
            core::log(core::LogLevel::error, restored.error().message);
            return 1;
        }

        core::log(core::LogLevel::info, "Snapshot build objects: " +
                                            std::to_string(snapshot.value().build_pieces.size()));
        core::log(core::LogLevel::info,
                  "Restored chunks: " + std::to_string(restored.value().stats().chunk_count));
        return 0;
    });
}
