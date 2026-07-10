#include "engine/assemblies/assembly.hpp"
#include "engine/save/save_binary_codec.hpp"
#include "engine/save/save_text_codec.hpp"

#include <cassert>
#include <string>

namespace {

[[nodiscard]] heartstead::core::PrototypeId id(std::string value) {
    auto parsed = heartstead::core::PrototypeId::parse(value);
    assert(parsed);
    return *parsed;
}

heartstead::assemblies::AssemblyDefinition definition() {
    using namespace heartstead;
    assemblies::AssemblyDefinition result;
    result.prototype_id = id("test:assemblies/kiln");
    result.construction_stages = {"footing", "shell"};
    assemblies::AssemblyPartRequirement firebox{"firebox", id("test:pieces/firebox"), false};
    firebox.construction_stage = 0;
    firebox.role = "heat_core";
    assemblies::AssemblyPartRequirement chimney{"chimney", id("test:pieces/chimney"), false};
    chimney.construction_stage = 1;
    chimney.relative_coord = {0, 1, 0};
    chimney.role = "smoke_path";
    result.part_requirements = {firebox, chimney};
    result.capabilities = {"drying", "firing"};
    result.allowed_processes = {id("test:processes/firing")};
    result.capacity = 8;
    return result;
}

void test_staged_blueprint_and_state_machine() {
    using namespace heartstead;
    auto prototype = definition();
    assert(prototype.validate());
    auto record = assemblies::AssemblyRuntime::create_blueprint(
        core::SaveId::from_value(10), core::SaveId::from_value(20), prototype);
    assert(record && record.value().state == assemblies::AssemblyState::blueprint);
    assert(assemblies::AssemblyRuntime::place_part(
        record.value(), prototype,
        {"firebox", core::SaveId::from_value(21), id("test:pieces/firebox")}));
    assert(assemblies::AssemblyRuntime::advance_stage(record.value(), prototype));
    assert(record.value().current_stage == 1);
    assert(assemblies::AssemblyRuntime::place_part(
        record.value(), prototype,
        {"chimney", core::SaveId::from_value(22), id("test:pieces/chimney")}));
    assert(assemblies::AssemblyRuntime::advance_stage(record.value(), prototype));
    assert(record.value().state == assemblies::AssemblyState::ready);
    assert(
        assemblies::AssemblyRuntime::transition(record.value(), assemblies::AssemblyState::drying));
    assert(assemblies::AssemblyRuntime::transition(record.value(),
                                                   assemblies::AssemblyState::maiden_firing));
    assert(
        assemblies::AssemblyRuntime::transition(record.value(), assemblies::AssemblyState::ready));
    assert(assemblies::AssemblyRuntime::attach_process(record.value(),
                                                       core::ProcessId::from_value(31)));
}

void test_assembly_state_save_round_trip() {
    using namespace heartstead;
    auto prototype = definition();
    auto record = assemblies::AssemblyRuntime::create_blueprint(
        core::SaveId::from_value(10), core::SaveId::from_value(20), prototype);
    assert(record);
    record.value().state = assemblies::AssemblyState::drying;
    record.value().current_stage = 2;
    record.value().revision = 9;
    record.value().process_slots = {core::ProcessId::from_value(31)};
    record.value().custom_state = "mortar=wet|batch=2";

    save::SaveSnapshot snapshot;
    snapshot.metadata.schema_version = 1;
    snapshot.metadata.game_version = "test";
    snapshot.assemblies.push_back(record.value());
    auto text =
        save::SaveTextCodec::decode_snapshot(save::SaveTextCodec::encode_snapshot(snapshot));
    assert(text && text.value().assemblies.front().state == assemblies::AssemblyState::drying);
    assert(text.value().assemblies.front().custom_state == "mortar=wet|batch=2");
    auto binary =
        save::SaveBinaryCodec::decode_snapshot(save::SaveBinaryCodec::encode_snapshot(snapshot));
    assert(binary && binary.value().assemblies.front().revision == 9);
    assert(binary.value().assemblies.front().process_slots.front() ==
           core::ProcessId::from_value(31));
}

} // namespace

int main() {
    test_staged_blueprint_and_state_machine();
    test_assembly_state_save_round_trip();
}
