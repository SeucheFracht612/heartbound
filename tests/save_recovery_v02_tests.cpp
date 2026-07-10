#include "engine/save/missing_prototype_recovery.hpp"
#include "engine/save/save_binary_codec.hpp"
#include "engine/save/save_text_codec.hpp"
#include "engine/world/world_snapshot.hpp"

#include <cassert>
#include <cstdint>
#include <string>

namespace {

[[nodiscard]] heartstead::core::PrototypeId id(std::string value) {
    auto parsed = heartstead::core::PrototypeId::parse(value);
    assert(parsed);
    return *parsed;
}

[[nodiscard]] heartstead::save::SaveSnapshot missing_mod_snapshot() {
    heartstead::save::SaveSnapshot snapshot;
    snapshot.metadata.schema_version = 1;
    snapshot.metadata.game_version = "test";
    snapshot.metadata.world_seed = 99;

    heartstead::build::BuildPieceRecord build;
    build.object_id = heartstead::core::SaveId::from_value(10);
    build.prototype_id = id("removedmod:build_pieces/storm_furnace");
    build.transform.position = heartstead::world::WorldPosition::from_anchor(
                                   {std::int64_t{1} << 60, -8, 3}, {0.25, 0.5, 0.75})
                                   .value();
    build.construction_state = heartstead::build::ConstructionState::complete;
    snapshot.build_pieces.push_back(build);

    auto stack = heartstead::items::ItemStack::create(id("removedmod:items/storm_ore"), 3, 64);
    assert(stack);
    snapshot.inventories.push_back({build.object_id, {stack.value()}});
    return snapshot;
}

void test_missing_objects_become_opaque_placeholders() {
    auto snapshot = missing_mod_snapshot();
    heartstead::modding::PrototypeRegistry empty_registry;
    assert(!empty_registry.build({}).has_errors());

    auto recovery = heartstead::save::preserve_missing_prototypes(snapshot, empty_registry);
    assert(recovery);
    assert(recovery.value().placeholder_count == 2);
    assert(snapshot.build_pieces.empty());
    assert(snapshot.inventories.empty());
    assert(snapshot.missing_prototypes.size() == 2);
    const auto& build_placeholder = snapshot.missing_prototypes.front();
    assert(build_placeholder.kind == heartstead::world::MissingPrototypeKind::build_piece);
    assert(build_placeholder.original_prototype_id ==
           id("removedmod:build_pieces/storm_furnace"));
    assert(build_placeholder.position.anchor.x == (std::int64_t{1} << 60));
    assert(build_placeholder.saved_blob.starts_with("heartstead.save_snapshot_text.v2\n"));
}

void test_placeholders_round_trip_and_load_without_mod() {
    auto source = missing_mod_snapshot();
    heartstead::modding::PrototypeRegistry empty_registry;
    assert(!empty_registry.build({}).has_errors());
    assert(heartstead::save::preserve_missing_prototypes(source, empty_registry));

    const auto text = heartstead::save::SaveTextCodec::encode_snapshot(source);
    auto text_round_trip = heartstead::save::SaveTextCodec::decode_snapshot(text);
    assert(text_round_trip);
    assert(text_round_trip.value().missing_prototypes.size() == 2);
    assert(text_round_trip.value().missing_prototypes.front().saved_blob ==
           source.missing_prototypes.front().saved_blob);

    const auto binary = heartstead::save::SaveBinaryCodec::encode_snapshot(source);
    auto binary_round_trip = heartstead::save::SaveBinaryCodec::decode_snapshot(binary);
    assert(binary_round_trip);
    assert(binary_round_trip.value().missing_prototypes.size() == 2);

    auto fresh_snapshot = missing_mod_snapshot();
    auto world = heartstead::world::WorldSnapshotBridge::import_validated_snapshot(
        fresh_snapshot, empty_registry);
    assert(world);
    assert(world.value().build_objects().count() == 0);
    assert(world.value().missing_prototypes().size() == 2);
    auto exported = heartstead::world::WorldSnapshotBridge::export_snapshot(world.value());
    assert(exported);
    assert(exported.value().missing_prototypes.size() == 2);
}

} // namespace

int main() {
    test_missing_objects_become_opaque_placeholders();
    test_placeholders_round_trip_and_load_without_mod();
    return 0;
}
