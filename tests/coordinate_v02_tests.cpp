#include "engine/build/build_piece.hpp"
#include "engine/dirty/dirty_region.hpp"
#include "engine/networks/network_derivation.hpp"
#include "engine/simulation/simulation_lod.hpp"
#include "engine/world/chunks/chunk_database.hpp"
#include "engine/world/coords/world_coords.hpp"
#include "engine/world/simulation_subjects.hpp"
#include "engine/world/world_state.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace {

using heartstead::world::BlockCoord;
using heartstead::world::ChunkCoord;
using heartstead::world::LocalCoord;

static_assert(std::is_same_v<decltype(BlockCoord::x), std::int64_t>);
static_assert(std::is_same_v<decltype(ChunkCoord::x), std::int64_t>);
static_assert(std::is_same_v<decltype(heartstead::dirty::DirtyRegionCoord::x), std::int64_t>);
static_assert(std::is_same_v<decltype(heartstead::simulation::SimulationCoord::x), std::int64_t>);
static_assert(std::is_same_v<decltype(heartstead::networks::NetworkCoord::x), std::int64_t>);
static_assert(heartstead::world::chunk_edge_length == 32);

void test_block_chunk_local_round_trips() {
    auto floor_negative = heartstead::world::floor_div_i64_checked(-33, 32);
    assert(floor_negative);
    assert(floor_negative.value() == -2);
    auto floor_mixed_sign = heartstead::world::floor_div_i64_checked(33, -32);
    assert(floor_mixed_sign);
    assert(floor_mixed_sign.value() == -2);
    assert(!heartstead::world::floor_div_i64_checked(1, 0));
    assert(!heartstead::world::floor_div_i64_checked(std::numeric_limits<std::int64_t>::min(), -1));

    constexpr auto negative_one = heartstead::world::block_to_chunk_local({-1, -32, -33});
    static_assert(negative_one.chunk == ChunkCoord{-1, -1, -2});
    static_assert(negative_one.local == LocalCoord{31, 0, 31});

    constexpr auto positive_edge = heartstead::world::block_to_chunk_local({31, 32, 33});
    static_assert(positive_edge.chunk == ChunkCoord{0, 1, 1});
    static_assert(positive_edge.local == LocalCoord{31, 0, 1});

    constexpr auto min = std::numeric_limits<std::int64_t>::min();
    constexpr auto max = std::numeric_limits<std::int64_t>::max();
    for (const BlockCoord block :
         {BlockCoord{0, 0, 0}, BlockCoord{-1, 31, -33}, BlockCoord{min, min + 1, min + 31},
          BlockCoord{max, max - 1, max - 31}}) {
        const auto split = heartstead::world::block_to_chunk_local(block);
        auto reconstructed = heartstead::world::chunk_local_to_block(split.chunk, split.local);
        assert(reconstructed);
        assert(reconstructed.value() == block);
    }

    auto invalid_local = heartstead::world::chunk_local_to_block({0, 0, 0}, {32, 0, 0});
    assert(!invalid_local);
    assert(invalid_local.error().code == "world_coord.invalid_local_coord");

    auto overflow = heartstead::world::chunk_local_to_block({max, 0, 0}, {});
    assert(!overflow);
    assert(overflow.error().code == "world_coord.block_coord_overflow");
}

void test_dirty_regions_keep_far_coordinates() {
    constexpr std::int64_t far = std::int64_t{1} << 40;
    heartstead::world::ChunkDatabase chunks;
    heartstead::dirty::DirtyRegionTracker dirty_regions;
    assert(chunks.set({far, -far, far + 7}, {1, 2, 3}, {1, 0}, dirty_regions));

    auto mesh_regions = dirty_regions.consume_kind(heartstead::dirty::DirtyRegionKind::chunk_mesh);
    assert(mesh_regions.size() == 1);
    const BlockCoord expected_far{far, -far, far + 7};
    assert(mesh_regions.front().bounds.min == expected_far);
    assert(mesh_regions.front().bounds.max == expected_far);

    constexpr auto min = std::numeric_limits<std::int64_t>::min();
    constexpr auto max = std::numeric_limits<std::int64_t>::max();
    const heartstead::dirty::DirtyRegionBounds extreme{{min, min, min}, {max, max, max}};
    auto expanded = extreme.expanded(std::numeric_limits<std::uint32_t>::max());
    assert(expanded);
    assert(expanded.value().min == extreme.min);
    assert(expanded.value().max == extreme.max);

    const auto max_cell = heartstead::dirty::DirtyRegionBounds::single({max, 0, 0});
    const auto max_neighbor = heartstead::dirty::DirtyRegionBounds::single({max - 1, 0, 0});
    assert(max_cell.overlaps_or_touches(max_neighbor));
}

void test_simulation_distance_at_i64_extremes() {
    using namespace heartstead::simulation;

    SimulationSubject subject;
    subject.save_id = heartstead::core::SaveId::from_value(1);
    subject.coord = {std::numeric_limits<std::int64_t>::min(), 0, 0};

    const std::vector<SimulationViewer> viewers{{
        heartstead::core::NetId::from_value(1),
        {std::numeric_limits<std::int64_t>::max(), 0, 0},
    }};
    auto decision = SimulationLodPlanner::classify(subject, viewers, {}, 0);
    assert(decision);
    assert(decision.value().nearest_viewer_distance_squared ==
           std::numeric_limits<std::uint64_t>::max());
    assert(decision.value().lod == SimulationLod::unloaded);
}

void test_far_transform_derivation() {
    constexpr std::int64_t far = std::int64_t{1} << 40;
    const auto prototype = heartstead::core::PrototypeId::parse("base:build_pieces/far_test");
    assert(prototype);

    heartstead::build::BuildPieceRecord positive;
    positive.object_id = heartstead::core::SaveId::from_value(1);
    positive.prototype_id = prototype.value();
    positive.transform.position = {static_cast<double>(far) + 0.75, 0.0, 0.0};
    positive.construction_state = heartstead::build::ConstructionState::complete;
    positive.network_ports.push_back(
        {"storage", heartstead::networks::NetworkKind::storage_access, 1});

    auto negative = positive;
    negative.object_id = heartstead::core::SaveId::from_value(2);
    negative.transform.position.x = -static_cast<double>(far) - 0.25;

    heartstead::networks::SpatialNetworkDerivationInput input;
    input.build_pieces = {&positive, &negative};
    auto derived = heartstead::networks::SpatialNetworkDeriver::derive(input);
    assert(derived);
    assert(derived.value().networks.size() == 1);
    const auto nodes = derived.value().networks.front().nodes();
    assert(nodes.size() == 2);
    assert(std::ranges::any_of(nodes, [far](const auto* node) { return node->coord.x == far; }));
    assert(
        std::ranges::any_of(nodes, [far](const auto* node) { return node->coord.x == -far - 1; }));

    heartstead::world::WorldState state;
    assert(state.build_objects().insert(positive));
    heartstead::world::WorldSimulationSubjectOptions options;
    options.include_entities = false;
    options.include_assemblies = false;
    options.include_processes = false;
    options.include_networks = false;
    options.include_chunk_regions = false;
    auto subjects = heartstead::world::derive_simulation_subjects(state, options);
    assert(subjects);
    assert(subjects.value().size() == 1);
    assert(subjects.value().front().coord.x == far);
}

} // namespace

int main() {
    test_block_chunk_local_round_trips();
    test_dirty_regions_keep_far_coordinates();
    test_simulation_distance_at_i64_extremes();
    test_far_transform_derivation();
    return 0;
}
