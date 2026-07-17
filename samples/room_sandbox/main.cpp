#include "engine/build/build_piece.hpp"
#include "engine/core/logging.hpp"
#include "engine/core/process_entry.hpp"
#include "engine/debug/inspection.hpp"
#include "engine/rooms/room_extraction.hpp"
#include "engine/rooms/room_graph.hpp"
#include "engine/world/voxels/voxel_chunk.hpp"

#include <string>
#include <utility>

namespace {

heartstead::rooms::RoomRecord make_smoky_kitchen() {
    heartstead::rooms::RoomRecord room;
    room.id = heartstead::rooms::RoomId::from_value(2);
    room.label = "Smoky Kitchen";
    room.volume_cells = 96;
    room.source_build_piece_ids.push_back(heartstead::core::SaveId::from_value(201));
    room.metrics.enclosure_per_mille = 740;
    room.metrics.roof_coverage_per_mille = 620;
    room.metrics.wall_coverage_per_mille = 700;
    room.metrics.warmth = 300;
    room.metrics.dryness = -180;
    room.metrics.light_per_mille = 220;
    room.metrics.smoke_per_mille = 760;
    room.metrics.ventilation_per_mille = 120;
    room.metrics.safety_per_mille = 420;
    room.metrics.spaciousness_per_mille = 280;
    return room;
}

heartstead::core::Result<heartstead::rooms::RoomGraph> make_extracted_room_graph() {
    auto builder_result = heartstead::rooms::RoomExtractionGridBuilder::create({5, 3, 5});
    if (!builder_result) {
        return heartstead::core::Result<heartstead::rooms::RoomGraph>::failure(
            builder_result.error().code, builder_result.error().message);
    }

    auto builder = std::move(builder_result).value();
    const auto prototype = heartstead::core::PrototypeId::parse("base:build_pieces/wall_frame");
    if (!prototype) {
        return heartstead::core::Result<heartstead::rooms::RoomGraph>::failure(
            "room_sandbox.invalid_prototype", "sample build-piece prototype id is invalid");
    }

    heartstead::build::BuildPieceRecord wall_piece;
    wall_piece.object_id = heartstead::core::SaveId::from_value(101);
    wall_piece.prototype_id = prototype.value();
    wall_piece.construction_state = heartstead::build::ConstructionState::complete;
    wall_piece.room_contribution_tags = {"wall", "enclosure", "warm", "dry"};

    heartstead::build::BuildPieceRecord roof_piece;
    roof_piece.object_id = heartstead::core::SaveId::from_value(102);
    roof_piece.prototype_id = prototype.value();
    roof_piece.construction_state = heartstead::build::ConstructionState::complete;
    roof_piece.room_contribution_tags = {"roof"};

    heartstead::build::BuildPieceRecord access_piece;
    access_piece.object_id = heartstead::core::SaveId::from_value(103);
    access_piece.prototype_id = prototype.value();
    access_piece.construction_state = heartstead::build::ConstructionState::complete;
    access_piece.network_ports.push_back(
        {"storage", heartstead::networks::NetworkKind::storage_access, 1});
    access_piece.network_ports.push_back(
        {"cart", heartstead::networks::NetworkKind::cart_access, 1});
    access_piece.network_ports.push_back({"power", heartstead::networks::NetworkKind::power, 1});

    for (std::uint16_t z = 0; z < 5; ++z) {
        for (std::uint16_t x = 0; x < 5; ++x) {
            auto status =
                builder.apply_terrain_voxel({x, 0, z}, heartstead::world::VoxelCell{1, 0});
            if (!status) {
                return heartstead::core::Result<heartstead::rooms::RoomGraph>::failure(
                    status.error().code, status.error().message);
            }
            status = builder.apply_build_piece(roof_piece, {x, 2, z});
            if (!status) {
                return heartstead::core::Result<heartstead::rooms::RoomGraph>::failure(
                    status.error().code, status.error().message);
            }
            if (x == 0 || x == 4 || z == 0 || z == 4) {
                status = builder.apply_build_piece(wall_piece, {x, 1, z});
                if (!status) {
                    return heartstead::core::Result<heartstead::rooms::RoomGraph>::failure(
                        status.error().code, status.error().message);
                }
            } else {
                status =
                    builder.apply_terrain_voxel({x, 1, z}, heartstead::world::VoxelCell{0, 194});
                if (!status) {
                    return heartstead::core::Result<heartstead::rooms::RoomGraph>::failure(
                        status.error().code, status.error().message);
                }
            }
        }
    }

    auto status = builder.apply_build_piece(access_piece, {2, 1, 2});
    if (!status) {
        return heartstead::core::Result<heartstead::rooms::RoomGraph>::failure(
            status.error().code, status.error().message);
    }

    return heartstead::rooms::RoomExtractor::extract(builder.grid());
}

void log_room(const heartstead::rooms::RoomRecord& room) {
    using heartstead::core::LogLevel;

    heartstead::core::log(LogLevel::info, "Room " + room.id.to_string() + ": " + room.label);
    for (const auto& descriptor : room.descriptors) {
        heartstead::core::log(
            LogLevel::info,
            "  " + std::string(descriptor.label) + " [" +
                std::string(heartstead::rooms::room_descriptor_severity_name(descriptor.severity)) +
                "]");
    }

    const auto inspection = heartstead::debug::Inspector::inspect(room);
    for (const auto& issue : inspection.issues) {
        heartstead::core::log(LogLevel::warning, "  " + issue.code + ": " + issue.message);
    }
}

} // namespace

int main(int argc, char** argv) {
    return heartstead::core::run_no_argument_process_entry(argc, argv, [] {
        using namespace heartstead;

        auto graph_result = make_extracted_room_graph();
        if (!graph_result) {
            core::log(core::LogLevel::error, graph_result.error().message);
            return 1;
        }
        auto graph = std::move(graph_result).value();

        auto smoky_room = make_smoky_kitchen();
        smoky_room.id = rooms::RoomId::from_value(2);
        auto status = graph.add_or_replace(smoky_room);
        if (!status) {
            core::log(core::LogLevel::error, status.error().message);
            return 1;
        }

        graph.evaluate_all();
        core::log(core::LogLevel::info, "Rooms evaluated: " + std::to_string(graph.room_count()));
        core::log(core::LogLevel::info,
                  "Smoky rooms: " + std::to_string(graph.count_descriptor("smoky")));
        core::log(core::LogLevel::info, "Rooms with cart access: " +
                                            std::to_string(graph.count_descriptor("cart_access")));

        for (const auto* room : graph.rooms()) {
            log_room(*room);
        }

        return graph.count_descriptor("smoky") == 1 ? 0 : 1;
    });
}
