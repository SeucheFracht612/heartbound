#include "engine/modding/mod_fingerprint.hpp"
#include "engine/net/command_payload.hpp"
#include "engine/net/server_command.hpp"
#include "engine/processes/process.hpp"
#include "engine/save/save_compatibility.hpp"
#include "engine/world/chunks/chunk_database.hpp"
#include "engine/world/chunks/chunk_edit_delta_codec.hpp"
#include "engine/world/streaming/chunk_streamer.hpp"
#include "engine/world/voxels/voxel_palette.hpp"
#include "engine/world/world_commands.hpp"
#include "engine/world/world_state.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

struct TerrainFixture {
    heartstead::world::VoxelPalette palette;
    heartstead::world::RegionGraph regions;
    heartstead::world::TerrainGenerationConfig generation;

    TerrainFixture() {
        auto clay_id = heartstead::core::PrototypeId::parse("base:voxels/clay");
        assert(clay_id);

        heartstead::world::VoxelDefinition clay;
        clay.type = 1;
        clay.prototype_id = clay_id.value();
        clay.display_name = "Clay";
        clay.terrain_material = "clay";
        clay.mining_tool = "shovel";
        assert(palette.add(std::move(clay)));

        heartstead::world::RegionDescriptor region;
        region.id = "integrity_test_region";
        region.age = "test_age";
        region.biome_cluster = "test_biome";
        region.resource_rules.push_back({clay_id.value(), "terrain", 1.0});
        assert(regions.add_region(std::move(region)));

        generation.world_seed = 42;
        generation.region_id = "integrity_test_region";
        generation.base_surface_y = 8;
    }
};

struct TestDeltaSource final : heartstead::world::IChunkEditDeltaSource {
    std::optional<heartstead::save::ChunkEditSaveRecord> record;
    bool fail_read = false;

    [[nodiscard]] heartstead::core::Result<std::optional<heartstead::save::ChunkEditSaveRecord>>
    read_chunk_delta(heartstead::world::ChunkCoord coord) const override {
        if (fail_read) {
            return heartstead::core::Result<std::optional<heartstead::save::ChunkEditSaveRecord>>::
                failure("test.delta_read_failed", "injected delta read failure");
        }
        if (!record.has_value() || record->coord != coord) {
            return heartstead::core::Result<
                std::optional<heartstead::save::ChunkEditSaveRecord>>::success(std::nullopt);
        }
        return heartstead::core::Result<
            std::optional<heartstead::save::ChunkEditSaveRecord>>::success(record);
    }
};

[[nodiscard]] heartstead::save::ChunkEditSaveRecord
encoded_delta(heartstead::world::VoxelEditRecord& edit) {
    const std::vector<const heartstead::world::VoxelEditRecord*> edits{&edit};
    return {edit.chunk_coord,
            heartstead::world::ChunkEditDeltaTextCodec::encode(edit.chunk_coord, edits)};
}

void test_failed_stream_load_does_not_publish_chunk() {
    TerrainFixture fixture;
    heartstead::world::WorldState state;
    TestDeltaSource source;
    source.fail_read = true;

    const heartstead::world::ChunkCoord read_failure_coord{4, 0, 0};
    auto read_failure = heartstead::world::ChunkStreamer::load_chunk(
        state, read_failure_coord, fixture.generation, fixture.regions, fixture.palette, &source);
    assert(!read_failure);
    assert(!state.chunks().contains(read_failure_coord));

    source.fail_read = false;
    const heartstead::world::ChunkCoord decode_failure_coord{5, 0, 0};
    source.record =
        heartstead::save::ChunkEditSaveRecord{decode_failure_coord, "not-a-heartstead-chunk-delta"};
    auto decode_failure = heartstead::world::ChunkStreamer::load_chunk(
        state, decode_failure_coord, fixture.generation, fixture.regions, fixture.palette, &source);
    assert(!decode_failure);
    assert(!state.chunks().contains(decode_failure_coord));

    const heartstead::world::ChunkCoord bounds_failure_coord{6, 0, 0};
    source.record = heartstead::save::ChunkEditSaveRecord{
        bounds_failure_coord,
        "heartstead.chunk_edit_delta.v1\ncoord=6|0|0\nedit=32|0|0|0|0|1|0\nend\n"};
    auto bounds_failure = heartstead::world::ChunkStreamer::load_chunk(
        state, bounds_failure_coord, fixture.generation, fixture.regions, fixture.palette, &source);
    assert(!bounds_failure);
    assert(bounds_failure.error().code == "world_snapshot.chunk_delta_voxel_out_of_bounds");
    assert(!state.chunks().contains(bounds_failure_coord));
}

void test_worldgen_preserves_i64_height_and_rejects_unrepresentable_chunks() {
    TerrainFixture fixture;
    constexpr std::int64_t far_height = std::int64_t{1} << 50;
    fixture.generation.base_surface_y = far_height;
    assert(heartstead::world::DeterministicTerrainGenerator::surface_height_at(
               fixture.generation, -9, 17) == far_height);

    const heartstead::world::ChunkCoord impossible{std::numeric_limits<std::int64_t>::max(), 0, 0};
    auto generated = heartstead::world::DeterministicTerrainGenerator::generate_chunk(
        impossible, fixture.generation, fixture.regions, fixture.palette);
    assert(!generated);
    assert(generated.error().code == "terrain_generator.chunk_coord_overflow");
}

void test_chunk_interest_planning_is_bounded() {
    heartstead::world::WorldState state;
    heartstead::world::ChunkStreamInterestPolicy policy;
    policy.load_radius_chunks = static_cast<std::uint16_t>(
        heartstead::world::ChunkStreamInterestPolicy::max_load_radius_chunks + 1U);
    policy.retain_radius_chunks = policy.load_radius_chunks;
    auto plan = heartstead::world::ChunkStreamer::plan_interest(state, {}, policy);
    assert(!plan);
    assert(plan.error().code == "chunk_stream.load_radius_too_large");
}

void test_saved_edit_batch_validation_is_atomic() {
    heartstead::world::ChunkDatabase chunks;
    const heartstead::world::ChunkCoord coord{7, 0, 0};
    chunks.get_or_create(coord).clear_all_dirty();

    const std::vector<heartstead::world::VoxelEditRecord> edits{
        {coord, {1, 2, 3}, heartstead::world::VoxelCell::air(), {2, 0}},
        {coord,
         {heartstead::world::VoxelChunk::edge_length, 0, 0},
         heartstead::world::VoxelCell::air(),
         {3, 0}},
    };

    auto status = chunks.apply_saved_edits(edits);
    assert(!status);
    assert(status.error().code == "chunk_database.saved_edit_coord_out_of_bounds");
    auto unchanged = chunks.get(coord, {1, 2, 3});
    assert(unchanged);
    assert(unchanged.value().is_air());
    assert(chunks.edit_log().empty());
}

void test_stream_reload_preserves_one_canonical_delta() {
    TerrainFixture fixture;
    heartstead::world::WorldState state;
    heartstead::world::VoxelEditRecord saved_edit{
        {8, 0, 0}, {1, 2, 3}, heartstead::world::VoxelCell::air(), {4, 0}};
    TestDeltaSource source;
    source.record = encoded_delta(saved_edit);

    auto first_load = heartstead::world::ChunkStreamer::load_chunk(
        state, saved_edit.chunk_coord, fixture.generation, fixture.regions, fixture.palette,
        &source);
    assert(first_load);
    assert(state.chunks().edit_log().size() == 1);

    const std::vector<heartstead::world::ChunkCoord> evict{saved_edit.chunk_coord};
    auto first_eviction = heartstead::world::ChunkStreamer::evict_chunks(state, evict);
    assert(first_eviction.evicted_count() == 1);

    auto second_load = heartstead::world::ChunkStreamer::load_chunk(
        state, saved_edit.chunk_coord, fixture.generation, fixture.regions, fixture.palette,
        &source);
    assert(second_load);
    assert(state.chunks().edit_log().size() == 1);

    assert(state.chunks().set(saved_edit.chunk_coord, {2, 2, 3}, {5, 0}));
    assert(state.chunks().edit_log().size() == 2);
}

void test_world_set_voxel_rejects_unloaded_chunk() {
    heartstead::world::WorldState state;
    heartstead::net::ServerCommandDispatcher dispatcher;
    assert(heartstead::world::WorldCommandRegistry::register_engine_commands(dispatcher));

    heartstead::net::CommandPayload payload;
    assert(payload.set("chunk", "9|0|0"));
    assert(payload.set("voxel", "1|2|3"));
    assert(payload.set("cell", "6|0"));

    heartstead::net::CommandEnvelope command;
    command.sequence = 1;
    command.sender = heartstead::core::NetId::from_value(2);
    command.type = "world.set_voxel";
    command.payload = heartstead::net::CommandPayloadTextCodec::encode(payload);

    heartstead::net::CommandExecutionContext context;
    context.executor_role = heartstead::net::CommandExecutorRole::authoritative_server;
    context.world_state = &state;

    auto rejected = dispatcher.dispatch(command, context);
    assert(!rejected);
    assert(rejected.error().code == "world_command.chunk_not_loaded");
    assert(!state.chunks().contains({9, 0, 0}));

    heartstead::world::VoxelChunk loaded({9, 0, 0});
    assert(state.chunks().insert_generated(std::move(loaded)));
    auto accepted = dispatcher.dispatch(command, context);
    assert(accepted);
}

void test_process_advance_all_is_atomic() {
    auto process_prototype = heartstead::core::PrototypeId::parse("base:processes/test");
    assert(process_prototype);

    heartstead::world::ProcessDatabase processes;
    for (std::uint64_t value = 1; value <= 2; ++value) {
        auto instance = heartstead::processes::ProcessRuntime::create(
            heartstead::core::ProcessId::from_value(value),
            heartstead::core::SaveId::from_value(100 + value), process_prototype.value(), 0,
            10'000);
        assert(instance);
        assert(processes.insert(std::move(instance).value()));
    }

    std::size_t resolution_count = 0;
    auto advanced = processes.advance_all(
        1'000, [&resolution_count](const heartstead::processes::ProcessInstance&) {
            ++resolution_count;
            if (resolution_count == 2) {
                return heartstead::core::Result<heartstead::processes::ProcessModifiers>::failure(
                    "test.modifier_failed", "injected modifier failure");
            }
            return heartstead::core::Result<heartstead::processes::ProcessModifiers>::success({});
        });
    assert(!advanced);
    assert(resolution_count == 2);
    for (const auto* process : processes.records()) {
        assert(process->last_update_time_ms == 0);
        assert(process->accumulated_effective_work_ms == 0);
        assert(process->state == heartstead::processes::ProcessState::running);
    }
}

void test_palette_fingerprint_changes_are_blocking() {
    heartstead::save::SaveMetadata metadata;
    metadata.game_version = "test";
    metadata.enabled_mods.push_back({"base", "1", "saved-hash"});

    std::vector<heartstead::modding::ModPrototypeFingerprint> active{
        {"base", "1", "changed-hash", 1}};
    auto changed = heartstead::save::SaveCompatibilityChecker::compare(metadata, active);
    assert(changed.has_errors());
    assert(std::ranges::any_of(changed.issues, [](const auto& issue) {
        return issue.code == "save_compatibility.prototype_hash_mismatch" &&
               issue.severity == heartstead::save::SaveCompatibilitySeverity::error;
    }));

    active = {{"base", "1", "saved-hash", 1}, {"extra", "1", "extra-hash", 1}};
    auto extra = heartstead::save::SaveCompatibilityChecker::compare(metadata, active);
    assert(extra.has_errors());
    assert(std::ranges::any_of(extra.issues, [](const auto& issue) {
        return issue.code == "save_compatibility.extra_active_mod" &&
               issue.severity == heartstead::save::SaveCompatibilitySeverity::error;
    }));
}

} // namespace

int main() {
    test_failed_stream_load_does_not_publish_chunk();
    test_worldgen_preserves_i64_height_and_rejects_unrepresentable_chunks();
    test_chunk_interest_planning_is_bounded();
    test_saved_edit_batch_validation_is_atomic();
    test_stream_reload_preserves_one_canonical_delta();
    test_world_set_voxel_rejects_unloaded_chunk();
    test_process_advance_all_is_atomic();
    test_palette_fingerprint_changes_are_blocking();
    return 0;
}
