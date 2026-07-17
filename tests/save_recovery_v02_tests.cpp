#include "engine/entities/physical_resource.hpp"
#include "engine/save/missing_prototype_recovery.hpp"
#include "engine/save/save_binary_codec.hpp"
#include "engine/save/save_text_codec.hpp"
#include "engine/workpieces/workpiece_codec.hpp"
#include "engine/workpieces/workpiece_state.hpp"
#include "engine/world/world_snapshot.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <string>
#include <utility>

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
    assert(build_placeholder.original_prototype_id == id("removedmod:build_pieces/storm_furnace"));
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
    auto trailing = heartstead::save::SaveTextCodec::decode_snapshot(text + "ignored=true\n");
    assert(!trailing && trailing.error().code == "save_text.trailing_data");
    assert(text_round_trip.value().missing_prototypes.size() == 2);
    assert(text_round_trip.value().missing_prototypes.front().saved_blob ==
           source.missing_prototypes.front().saved_blob);

    const auto binary = heartstead::save::SaveBinaryCodec::encode_snapshot(source);
    assert(binary);
    auto binary_round_trip = heartstead::save::SaveBinaryCodec::decode_snapshot(binary.value());
    assert(binary_round_trip);
    assert(binary_round_trip.value().missing_prototypes.size() == 2);

    auto fresh_snapshot = missing_mod_snapshot();
    auto world = heartstead::world::WorldSnapshotBridge::import_validated_snapshot(fresh_snapshot,
                                                                                   empty_registry);
    assert(world);
    assert(world.value().build_objects().count() == 0);
    assert(world.value().missing_prototypes().size() == 2);
    auto exported = heartstead::world::WorldSnapshotBridge::export_snapshot(world.value());
    assert(exported);
    assert(exported.value().missing_prototypes.size() == 2);
}

void test_recovery_does_not_hide_wrong_kind_prototypes() {
    auto snapshot = missing_mod_snapshot();
    heartstead::modding::GenericPrototype wrong_kind;
    wrong_kind.kind = heartstead::modding::PrototypeKinds::item;
    wrong_kind.id = id("removedmod:build_pieces/storm_furnace");
    wrong_kind.display_name = "Wrong-kind collision";
    heartstead::modding::GenericPrototype item;
    item.kind = heartstead::modding::PrototypeKinds::item;
    item.id = id("removedmod:items/storm_ore");
    item.display_name = "Storm ore";
    heartstead::modding::PrototypeRegistry registry;
    assert(!registry.build({std::move(wrong_kind), std::move(item)}).has_errors());

    const auto recovery = heartstead::save::preserve_missing_prototypes(snapshot, registry);
    assert(recovery);
    assert(snapshot.build_pieces.size() == 1);
    assert(snapshot.inventories.size() == 1);
    assert(snapshot.missing_prototypes.empty());

    const auto imported =
        heartstead::world::WorldSnapshotBridge::import_validated_snapshot(snapshot, registry);
    assert(!imported);
    assert(imported.error().code == "prototype_registry.kind_mismatch");
}

void test_recovery_is_transactional_on_placeholder_conflict() {
    auto snapshot = missing_mod_snapshot();
    snapshot.missing_prototypes.push_back({heartstead::world::MissingPrototypeKind::build_piece,
                                           10,
                                           id("removedmod:build_pieces/storm_furnace"),
                                           snapshot.build_pieces.front().transform.position,
                                           {},
                                           "already preserved",
                                           "existing placeholder"});
    const auto before = heartstead::save::SaveTextCodec::encode_snapshot(snapshot);
    heartstead::modding::PrototypeRegistry empty_registry;
    assert(!empty_registry.build({}).has_errors());

    const auto recovery = heartstead::save::preserve_missing_prototypes(snapshot, empty_registry);
    assert(!recovery);
    assert(recovery.error().code == "save_recovery.placeholder_conflict");
    assert(heartstead::save::SaveTextCodec::encode_snapshot(snapshot) == before);
}

void test_nested_missing_prototypes_preserve_their_owning_records() {
    using heartstead::modding::GenericPrototype;
    using heartstead::modding::PrototypeKinds;

    const auto build_prototype = id("base:build_pieces/frame");
    const auto entity_prototype = id("base:entities/resource");
    const auto workpiece_prototype = id("base:workpieces/clay");
    const auto assembly_prototype = id("base:assemblies/workbench");
    const auto process_prototype = id("base:processes/crafting");
    const auto item_prototype = id("base:items/known");
    const auto missing_cargo = id("removedmod:cargo/log");
    const auto missing_material = id("removedmod:materials/clay");
    const auto missing_part = id("removedmod:build_pieces/tool_head");
    const auto missing_slot = id("removedmod:items/catalyst");

    const auto prototype = [](std::string_view kind, const heartstead::core::PrototypeId& value) {
        GenericPrototype result;
        result.kind = std::string(kind);
        result.id = value;
        result.display_name = value.value();
        return result;
    };
    heartstead::modding::PrototypeRegistry registry;
    assert(!registry
                .build({prototype(PrototypeKinds::build_piece, build_prototype),
                        prototype(PrototypeKinds::entity, entity_prototype),
                        prototype(PrototypeKinds::workpiece, workpiece_prototype),
                        prototype(PrototypeKinds::assembly, assembly_prototype),
                        prototype(PrototypeKinds::process, process_prototype),
                        prototype(PrototypeKinds::item, item_prototype)})
                .has_errors());

    heartstead::save::SaveSnapshot snapshot;
    snapshot.metadata.game_version = "test";
    snapshot.metadata.world_seed = 7;
    heartstead::build::BuildPieceRecord build;
    build.object_id = heartstead::core::SaveId::from_value(10);
    build.prototype_id = build_prototype;
    snapshot.build_pieces.push_back(build);

    heartstead::entities::PhysicalResourceRecord resource;
    resource.resource_id = heartstead::core::SaveId::from_value(20);
    resource.prototype_id = entity_prototype;
    resource.cargo_prototype_id = missing_cargo;
    resource.mass_grams = 1;
    resource.volume_milliliters = 1;
    resource.allowed_transport_modes =
        heartstead::cargo::CargoTransportModes::of({heartstead::cargo::CargoTransportMode::hand});
    resource.segments.push_back({});
    snapshot.entities.push_back({resource.resource_id,
                                 entity_prototype,
                                 heartstead::entities::EntityKind::temporary_physics,
                                 false,
                                 heartstead::entities::PhysicalResourceTextCodec::encode(resource),
                                 {}});

    auto grid = heartstead::workpieces::WorkpieceGrid::create({1, 1, 1});
    auto server_state = heartstead::workpieces::WorkpieceServerState::generate({1, 1, 1}, 1);
    assert(grid && server_state);
    snapshot.workpieces.emplace_back(
        heartstead::core::WorkpieceId::from_value(30), workpiece_prototype,
        heartstead::workpieces::WorkpieceGridShape{1, 1, 1},
        heartstead::workpieces::WorkpieceGridTextCodec::encode(grid.value()), missing_material,
        heartstead::workpieces::WorkpieceServerStateTextCodec::encode(server_state.value(),
                                                                      {1, 1, 1}),
        heartstead::core::NetId{}, 1, false);
    auto known_stack = heartstead::items::ItemStack::create(item_prototype, 1, 1);
    assert(known_stack);
    snapshot.inventories.push_back(
        {heartstead::core::SaveId::from_value(30), {known_stack.value()}});

    heartstead::assemblies::AssemblyRecord assembly;
    assembly.assembly_id = heartstead::core::SaveId::from_value(40);
    assembly.root_build_piece_id = build.object_id;
    assembly.prototype_id = assembly_prototype;
    assembly.parts.push_back({"tool", build.object_id, missing_part});
    snapshot.assemblies.push_back(assembly);

    auto process = heartstead::processes::ProcessRuntime::create(
        heartstead::core::ProcessId::from_value(1), build.object_id, process_prototype, 0, 10);
    assert(process);
    process.value().input_slots.push_back({missing_slot, 1});
    snapshot.processes.push_back(std::move(process).value());

    const auto recovery = heartstead::save::preserve_missing_prototypes(snapshot, registry);
    assert(recovery);
    assert(snapshot.build_pieces.size() == 1);
    assert(snapshot.entities.empty());
    assert(snapshot.workpieces.empty());
    assert(snapshot.inventories.empty());
    assert(snapshot.assemblies.empty());
    assert(snapshot.processes.empty());
    assert(snapshot.missing_prototypes.size() == 5);
    const auto has_placeholder = [&snapshot](heartstead::world::MissingPrototypeKind kind,
                                             const heartstead::core::PrototypeId& prototype_id) {
        return std::ranges::any_of(snapshot.missing_prototypes, [&](const auto& missing) {
            return missing.kind == kind && missing.original_prototype_id == prototype_id;
        });
    };
    assert(has_placeholder(heartstead::world::MissingPrototypeKind::entity, missing_cargo));
    assert(has_placeholder(heartstead::world::MissingPrototypeKind::workpiece, missing_material));
    assert(
        has_placeholder(heartstead::world::MissingPrototypeKind::inventory, workpiece_prototype));
    assert(has_placeholder(heartstead::world::MissingPrototypeKind::assembly, missing_part));
    assert(has_placeholder(heartstead::world::MissingPrototypeKind::process, missing_slot));
}

} // namespace

int main() {
    test_missing_objects_become_opaque_placeholders();
    test_placeholders_round_trip_and_load_without_mod();
    test_recovery_does_not_hide_wrong_kind_prototypes();
    test_recovery_is_transactional_on_placeholder_conflict();
    test_nested_missing_prototypes_preserve_their_owning_records();
    return 0;
}
