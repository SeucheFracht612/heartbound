#include "engine/entities/entity_world.hpp"
#include "engine/simulation/tick_events.hpp"

#include <cassert>
#include <cstdint>
#include <string>

using namespace heartstead;

namespace {

core::PrototypeId prototype(std::string_view value) {
    auto parsed = core::PrototypeId::parse(value);
    assert(parsed.has_value());
    return *parsed;
}

void test_generational_handle_round_trip() {
    const auto id = entities::EntityId::from_parts(17, 9);
    assert(id.is_valid());
    assert(id.index() == 17);
    assert(id.generation() == 9);
    assert(entities::EntityId::from_value(id.value()) == id);
    assert(!entities::EntityId::from_parts(17, 0).is_valid());
}

void test_entity_lifecycle_and_sparse_components() {
    entities::EntityWorld world;
    simulation::TickEvents events;
    assert(events.begin_tick(1));

    auto first = world.create_entity(prototype("base:entities/player"));
    auto second = world.create_entity(prototype("base:entities/deer"));
    assert(first && second);
    assert(first.value() != second.value());

    auto first_transform = world.emplace<entities::TransformComponent>(first.value());
    auto first_health = world.emplace<entities::HealthComponent>(
        first.value(), entities::HealthComponent{80.0F, 100.0F});
    auto second_health = world.emplace<entities::HealthComponent>(
        second.value(), entities::HealthComponent{30.0F, 30.0F});
    assert(first_transform && first_health && second_health);
    assert(!world.emplace<entities::HealthComponent>(first.value()));
    assert(world.component_store<entities::HealthComponent>().size() == 2);

    assert(world.activate_entity(first.value(), &events));
    assert(world.activate_entity(second.value(), &events));
    assert(events.entity_spawned.size() == 2);
    assert(world.stats().active_entities == 2);

    assert(world.deactivate_entity(second.value()));
    assert(world.activate_entity(second.value(), &events));
    assert(events.entity_spawned.size() == 2);
    assert(world.find_component<entities::HealthComponent>(first.value())->current == 80.0F);
}

void test_centralized_destruction_removes_components_and_reuses_generation() {
    entities::EntityWorld world;
    std::uint32_t cleanup_count = 0;
    assert(world.register_cleanup(
        "physics", [&cleanup_count](entities::EntityId, const entities::EntityWorldRecord&) {
            ++cleanup_count;
            return core::Status::ok();
        }));

    auto id = world.create_entity(prototype("base:entities/cart"));
    assert(id);
    assert(world.emplace<entities::PhysicsBodyComponent>(
        id.value(), entities::PhysicsBodyComponent{55}));
    assert(world.emplace<entities::HealthComponent>(id.value()));
    assert(world.activate_entity(id.value()));
    assert(world.destroy_entity(id.value()));
    assert(world.destroy_entity(id.value()));
    assert(world.stats().pending_destruction == 1);
    assert(!world.emplace<entities::VisualComponent>(id.value()));

    simulation::TickEvents events;
    assert(events.begin_tick(8));
    auto finalized = world.finalize_destruction(8, &events);
    assert(finalized && finalized.value() == 1);
    assert(cleanup_count == 1);
    assert(events.entity_destroyed.size() == 1);
    assert(!world.is_alive(id.value()));
    assert(world.find_component<entities::HealthComponent>(id.value()) == nullptr);
    assert(world.component_store<entities::PhysicsBodyComponent>().size() == 0);
    assert(world.tombstones().size() == 1);
    assert(world.tombstones().front().id == id.value());
    assert(world.tombstones().front().destroyed_tick == 8);

    auto replacement = world.create_entity(prototype("base:entities/cart"));
    assert(replacement);
    assert(replacement.value().index() == id.value().index());
    assert(replacement.value().generation() == id.value().generation() + 1);
    assert(!world.is_alive(id.value()));
}

void test_cleanup_failure_preserves_pending_entity() {
    entities::EntityWorld world;
    assert(world.register_cleanup(
        "failing_domain_store", [](entities::EntityId, const entities::EntityWorldRecord&) {
            return core::Status::failure("test.cleanup", "retry later");
        }));
    auto id = world.create_entity(prototype("base:entities/item"));
    assert(id);
    assert(world.emplace<entities::HealthComponent>(id.value()));
    assert(world.destroy_entity(id.value()));

    auto result = world.finalize_destruction(1);
    assert(!result);
    assert(result.error().code == "entity_world.cleanup_failed");
    assert(world.is_alive(id.value()));
    assert(world.find(id.value())->lifecycle == entities::EntityLifecycle::pending_destruction);
    assert(world.find_component<entities::HealthComponent>(id.value()) != nullptr);
    assert(world.tombstones().empty());
}

void test_component_batches_are_contiguous() {
    entities::EntityWorld world;
    for (std::uint32_t index = 0; index < 128; ++index) {
        auto id = world.create_entity(prototype("base:entities/creature"));
        assert(id);
        assert(world.emplace<entities::HealthComponent>(
            id.value(), entities::HealthComponent{static_cast<float>(index), 200.0F}));
    }
    auto& store = world.component_store<entities::HealthComponent>();
    assert(store.entities().size() == 128);
    assert(store.components().size() == 128);
    float sum = 0.0F;
    for (auto& health : store.components()) {
        health.current += 1.0F;
        sum += health.current;
    }
    assert(sum > 8000.0F);
}

} // namespace

int main() {
    test_generational_handle_round_trip();
    test_entity_lifecycle_and_sparse_components();
    test_centralized_destruction_removes_components_and_reuses_generation();
    test_cleanup_failure_preserves_pending_entity();
    test_component_batches_are_contiguous();
    return 0;
}
