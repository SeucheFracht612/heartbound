#include "engine/simulation/fixed_step.hpp"
#include "engine/simulation/simulation_scheduler.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <string>
#include <vector>

using namespace heartstead;

namespace {

void test_fixed_step_clock_is_bounded_and_deterministic() {
    simulation::FixedStepClock clock({60, 4, 250'000});

    auto first = clock.advance(16'666);
    assert(first);
    assert(first.value().step_count == 0);
    assert(first.value().interpolation_alpha > 0.99);

    auto second = clock.advance(1);
    assert(second);
    assert(second.value().step_count == 1);
    assert(second.value().first_tick == 1);
    assert(clock.tick() == 1);
    assert(second.value().interpolation_alpha < 0.001);

    auto stalled = clock.advance(1'000'000);
    assert(stalled);
    assert(stalled.value().step_count == 4);
    assert(stalled.value().dropped_time_us >= 750'000);
    assert(stalled.value().interpolation_alpha >= 0.0);
    assert(stalled.value().interpolation_alpha < 1.0);
}

void test_tick_events_have_explicit_lifetime() {
    simulation::TickEvents events;
    assert(events.begin_tick(7));
    assert(!events.begin_tick(8));

    simulation::EntitySpawned spawned;
    spawned.entity = core::RuntimeHandle::from_value(42);
    spawned.prototype = *core::PrototypeId::parse("base:entities/player");
    assert(events.entity_spawned.append(spawned));
    assert(events.event_count() == 1);
    assert(events.seal());
    assert(!events.entity_spawned.append(spawned));
    assert(events.entity_spawned.events().front().entity == spawned.entity);

    events.clear();
    assert(events.event_count() == 0);
    assert(!events.is_active());
    assert(events.begin_tick(8));
    assert(events.tick() == 8);
}

void test_scheduler_orders_phases_and_dependencies() {
    simulation::SimulationScheduler scheduler;
    std::vector<std::string> order;

    assert(scheduler.register_system({
        "gameplay.second",
        simulation::SimulationPhase::gameplay,
        {"gameplay.first"},
        [&order](simulation::SimulationContext&) {
            order.emplace_back("gameplay.second");
            return core::Status::ok();
        },
    }));
    assert(scheduler.register_system({
        "commands",
        simulation::SimulationPhase::commands,
        {},
        [&order](simulation::SimulationContext&) {
            order.emplace_back("commands");
            return core::Status::ok();
        },
    }));
    assert(scheduler.register_system({
        "gameplay.first",
        simulation::SimulationPhase::gameplay,
        {"commands"},
        [&order](simulation::SimulationContext& context) {
            order.emplace_back("gameplay.first");
            simulation::InventoryChanged changed;
            changed.inventory = core::SaveId::from_value(9);
            changed.revision = context.tick;
            return context.events->inventory_changed.append(changed);
        },
    }));
    assert(scheduler.finalize());

    const auto names = scheduler.ordered_system_names();
    assert((names == std::vector<std::string_view>{"commands", "gameplay.first",
                                                   "gameplay.second"}));

    simulation::TickEvents events;
    auto tick = scheduler.run_tick({11, 1.0 / 60.0, nullptr, nullptr, &events});
    assert(tick);
    assert((order == std::vector<std::string>{"commands", "gameplay.first",
                                              "gameplay.second"}));
    assert(tick.value().tick == 11);
    assert(tick.value().system_count == 3);
    assert(tick.value().event_count == 1);
    assert(events.is_sealed());
    assert(scheduler.timings().size() == 3);
    assert(scheduler.timings()[0].invocation_count == 1);
}

void test_scheduler_rejects_invalid_graphs_and_reports_failures() {
    simulation::SimulationScheduler cycle;
    assert(cycle.register_system({"a", simulation::SimulationPhase::gameplay, {"b"},
                                  [](simulation::SimulationContext&) {
                                      return core::Status::ok();
                                  }}));
    assert(cycle.register_system({"b", simulation::SimulationPhase::gameplay, {"a"},
                                  [](simulation::SimulationContext&) {
                                      return core::Status::ok();
                                  }}));
    assert(!cycle.finalize());

    simulation::SimulationScheduler scheduler;
    assert(scheduler.register_system({
        "failure", simulation::SimulationPhase::gameplay, {},
        [](simulation::SimulationContext&) {
            return core::Status::failure("test.failure", "intentional failure");
        }}));
    assert(scheduler.finalize());
    simulation::TickEvents events;
    auto result = scheduler.run_tick({1, 0.05, nullptr, nullptr, &events});
    assert(!result);
    assert(result.error().code == "simulation_scheduler.system_failed");
    assert(events.is_sealed());
}

} // namespace

int main() {
    test_fixed_step_clock_is_bounded_and_deterministic();
    test_tick_events_have_explicit_lifetime();
    test_scheduler_orders_phases_and_dependencies();
    test_scheduler_rejects_invalid_graphs_and_reports_failures();
    return 0;
}
