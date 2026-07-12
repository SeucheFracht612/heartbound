#include "engine/modding/prototype_registry.hpp"
#include "engine/workpieces/pattern_library.hpp"
#include "engine/workpieces/workpiece_codec.hpp"
#include "engine/workpieces/workpiece_state.hpp"
#include "engine/world/replication_delta.hpp"
#include "engine/world/world_state.hpp"

#include <cassert>
#include <string>

namespace {

[[nodiscard]] heartstead::core::PrototypeId id(std::string value) {
    auto parsed = heartstead::core::PrototypeId::parse(value);
    assert(parsed);
    return *parsed;
}

heartstead::workpieces::WorkpieceGrid patterned_grid() {
    auto grid = heartstead::workpieces::WorkpieceGrid::create({2, 1, 2});
    assert(grid);
    assert(grid.value().apply(
        {heartstead::workpieces::WorkpieceOperationKind::add_cell, {0, 0, 0}, {7, 255, 4, 3}}));
    assert(grid.value().apply({heartstead::workpieces::WorkpieceOperationKind::add_cell,
                               {1, 0, 0},
                               heartstead::workpieces::WorkpieceCell::solid(7)}));
    return std::move(grid).value();
}

void test_grid_v2_round_trip() {
    auto grid = patterned_grid();
    auto decoded = heartstead::workpieces::WorkpieceGridTextCodec::decode(
        heartstead::workpieces::WorkpieceGridTextCodec::encode(grid));
    assert(decoded);
    auto cell = decoded.value().get({0, 0, 0});
    assert(cell && cell.value().pattern_id == 4 && cell.value().flags == 3);
    assert(decoded.value().mesh_revision() == 2);
}

heartstead::workpieces::PatternLibrary pattern_library() {
    heartstead::workpieces::PatternDefinition pattern;
    pattern.prototype_id = id("test:patterns/tool");
    pattern.output_prototype_id = id("test:items/tool");
    pattern.shape = {2, 1, 2};
    pattern.occupied_cells = {{0, 0, 0}, {1, 0, 0}};
    pattern.allow_rotate_y = true;
    pattern.allow_mirror_x = true;
    pattern.material_constraints = {id("test:materials/stone")};
    heartstead::workpieces::PatternLibrary result;
    assert(result.add(std::move(pattern)));
    return result;
}

void test_server_validation_and_metadata() {
    auto grid = patterned_grid();
    auto state = heartstead::workpieces::WorkpieceServerState::generate(grid.shape(), 99, 1000);
    assert(state);
    auto library = pattern_library();
    auto result = heartstead::workpieces::finish_workpiece(
        grid, &state.value(), library, id("test:materials/stone"), id("test:patterns/tool"));
    assert(result);
    assert(result.value().output_prototype_id == id("test:items/tool"));
    assert(result.value().metadata.mass_units == 2);
    assert(result.value().metadata.base_closed);
    assert(result.value().metadata.measurements.at("occupied_cells") == 2);
}

void test_hidden_flaws_are_redacted_from_replication() {
    using namespace heartstead;
    world::WorldState state;
    auto grid = patterned_grid();
    auto server_state = workpieces::WorkpieceServerState::generate(grid.shape(), 42, 1000);
    assert(server_state);
    const auto hidden_before = server_state.value().hidden_flaw_mask;
    assert(state.workpieces().insert(world::WorkpieceRecord{
        core::WorkpieceId::from_value(71), id("test:workpieces/blank"), std::move(grid),
        id("test:materials/stone"), std::move(server_state).value(), core::NetId::from_value(3), 4,
        false}));

    net::ReplicationBatch batch;
    batch.command_sequence = 1;
    batch.events.push_back({"workpiece.edited", core::SaveId::from_value(71), "server edit"});
    auto delta = world::materialize_replication_delta(state, batch);
    assert(delta.workpieces.size() == 1);
    auto public_state = workpieces::WorkpieceServerStateTextCodec::decode(
        delta.workpieces.front().encoded_server_state, {2, 1, 2});
    assert(public_state);
    assert(public_state.value().hidden_flaw_mask != hidden_before);
    for (const auto word : public_state.value().hidden_flaw_mask)
        assert(word == 0);

    world::WorldState client;
    auto applied = world::apply_replication_delta(client, delta);
    assert(applied && applied.value().workpieces_inserted == 1);
    assert(client.workpieces().find(core::WorkpieceId::from_value(71)) != nullptr);
}

void test_server_state_codec_rejects_wrong_shape() {
    auto state = heartstead::workpieces::WorkpieceServerState::generate({2, 1, 2}, 5);
    assert(state);
    const auto encoded =
        heartstead::workpieces::WorkpieceServerStateTextCodec::encode(state.value(), {2, 1, 2});
    auto wrong = heartstead::workpieces::WorkpieceServerStateTextCodec::decode(encoded, {3, 1, 2});
    assert(!wrong && wrong.error().code == "workpiece_state.shape_mismatch");
    state.value().output_metadata.classification = "pattern_match";
    state.value().output_metadata.measurements["occupied_cells"] = 4;
    auto round_trip = heartstead::workpieces::WorkpieceServerStateTextCodec::decode(
        heartstead::workpieces::WorkpieceServerStateTextCodec::encode(state.value(), {2, 1, 2}),
        {2, 1, 2});
    assert(round_trip);
    assert(round_trip.value().output_metadata.measurements.at("occupied_cells") == 4);

    state.value().blob_mask.back() |= std::uint64_t{1} << 63U;
    assert(!state.value().validate({2, 1, 2}));
}

} // namespace

int main() {
    test_grid_v2_round_trip();
    test_server_validation_and_metadata();
    test_hidden_flaws_are_redacted_from_replication();
    test_server_state_codec_rejects_wrong_shape();
}
