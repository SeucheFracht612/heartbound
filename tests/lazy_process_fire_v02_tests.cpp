#include "engine/net/command_payload.hpp"
#include "engine/processes/process.hpp"
#include "engine/save/save_binary_codec.hpp"
#include "engine/save/save_text_codec.hpp"
#include "engine/simulation/fire.hpp"
#include "engine/world/world_commands.hpp"
#include "engine/world/world_state.hpp"

#include <cassert>
#include <string>

namespace {

[[nodiscard]] heartstead::core::PrototypeId id(std::string value) {
    auto parsed = heartstead::core::PrototypeId::parse(value);
    assert(parsed);
    return *parsed;
}

void test_processes_evaluate_lazily_from_world_ticks() {
    using namespace heartstead;
    world::ProcessDatabase database;
    auto first = processes::ProcessRuntime::create(core::ProcessId::from_value(1),
                                                   core::SaveId::from_value(10),
                                                   id("base:processes/drying"), 100, 1'000);
    auto second = processes::ProcessRuntime::create(core::ProcessId::from_value(2),
                                                    core::SaveId::from_value(20),
                                                    id("base:processes/drying"), 100, 1'000);
    assert(first && second);
    first.value().condition_function_id = "drying_conditions";
    first.value().interruption_policy = processes::ProcessInterruptionPolicy::reset;
    assert(database.insert(first.value()));
    assert(database.insert(second.value()));

    auto evaluated = database.evaluate_owner(
        core::SaveId::from_value(10), 600, processes::ProcessEvaluationTrigger::chunk_load,
        [](const auto&) { return core::Result<processes::ProcessModifiers>::success({}); });
    assert(evaluated && evaluated.value() == 1);
    assert(database.find(core::ProcessId::from_value(1))->accrued_work_ticks == 500);
    assert(database.find(core::ProcessId::from_value(2))->accrued_work_ticks == 0);

    processes::ProcessRuntime::interrupt(*database.find(core::ProcessId::from_value(1)),
                                         "weather_changed");
    assert(database.find(core::ProcessId::from_value(1))->accrued_work_ticks == 0);
}

void test_sleep_advances_only_authoritative_world_time() {
    using namespace heartstead;
    world::WorldState state;
    net::ServerCommandDispatcher dispatcher;
    assert(world::WorldCommandRegistry::register_engine_commands(dispatcher));

    net::CommandPayload payload;
    assert(payload.set("hours", "2"));
    net::CommandEnvelope command;
    command.sequence = 1;
    command.sender = core::NetId::from_value(2);
    command.type = "world.sleep";
    command.payload = net::CommandPayloadTextCodec::encode(payload);
    net::CommandExecutionContext context;
    context.world_state = &state;
    auto result = dispatcher.dispatch(command, context);
    assert(result);
    assert(state.world_time() == 144'000);
}

void test_fire_replays_elapsed_window_without_ticking() {
    using namespace heartstead;
    simulation::FireDefinition definition{id("base:fire/campfire"), 50, 200, 12, 5.0F, 8.0F, 2};
    auto fire = simulation::FireRuntime::create(core::SaveId::from_value(1), definition, 0, false);
    assert(fire);
    assert(simulation::FireRuntime::feed(fire.value(), definition, 100));
    assert(simulation::FireRuntime::ignite(fire.value(), definition, 0));
    assert(simulation::FireRuntime::evaluate(fire.value(), definition, 75));
    assert(fire.value().state == simulation::FireState::lit);
    assert(fire.value().fuel_buffer_ticks == 25);
    assert(simulation::FireRuntime::evaluate(fire.value(), definition, 200));
    assert(fire.value().state == simulation::FireState::out);

    auto exposed =
        simulation::FireRuntime::create(core::SaveId::from_value(2), definition, 0, true);
    assert(exposed);
    assert(simulation::FireRuntime::feed(exposed.value(), definition, 100));
    assert(simulation::FireRuntime::ignite(exposed.value(), definition, 0));
    assert(simulation::FireRuntime::evaluate(exposed.value(), definition, 40, {20}));
    assert(exposed.value().state == simulation::FireState::embers);
    assert(exposed.value().fuel_buffer_ticks == 80);
    assert(simulation::FireRuntime::evaluate(exposed.value(), definition, 80));
    assert(exposed.value().state == simulation::FireState::out);
}

void test_lazy_process_and_fire_save_round_trip() {
    using namespace heartstead;
    save::SaveSnapshot snapshot;
    snapshot.metadata.schema_version = 1;
    snapshot.metadata.game_version = "test";
    auto process = processes::ProcessRuntime::create(core::ProcessId::from_value(1),
                                                     core::SaveId::from_value(10),
                                                     id("base:processes/drying"), 5, 100);
    assert(process);
    process.value().condition_function_id = "drying_conditions";
    process.value().output_claimed = false;
    snapshot.processes.push_back(process.value());
    snapshot.fires.push_back({core::SaveId::from_value(10), id("base:fire/campfire"),
                              simulation::FireState::lit, 50, 5, 0, false});

    const auto text = save::SaveTextCodec::encode_snapshot(snapshot);
    auto text_decoded = save::SaveTextCodec::decode_snapshot(text);
    assert(text_decoded);
    assert(text_decoded.value().processes.front().started_at == 5);
    assert(text_decoded.value().processes.front().condition_function_id == "drying_conditions");
    assert(text_decoded.value().fires.front().fuel_buffer_ticks == 50);

    const auto binary = save::SaveBinaryCodec::encode_snapshot(snapshot);
    auto binary_decoded = save::SaveBinaryCodec::decode_snapshot(binary);
    assert(binary_decoded);
    assert(binary_decoded.value().processes.front().required_work_ticks == 100);
    assert(binary_decoded.value().fires.front().state == simulation::FireState::lit);
}

} // namespace

int main() {
    test_processes_evaluate_lazily_from_world_ticks();
    test_sleep_advances_only_authoritative_world_time();
    test_fire_replays_elapsed_window_without_ticking();
    test_lazy_process_and_fire_save_round_trip();
    return 0;
}
