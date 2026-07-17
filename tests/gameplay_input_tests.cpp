#include "engine/input/input_action.hpp"
#include "engine/world/voxels/voxel_chunk.hpp"
#include "game/features/interaction/voxel_raycast.hpp"

#include <cassert>

using namespace heartstead;

namespace {

void test_contextual_action_map_and_rebinding() {
    auto actions = input::InputActionMap::gameplay_defaults();
    platform::WindowInputSnapshot snapshot;
    snapshot.down_keys = {platform::KeyCode::w};
    snapshot.pressed_keys = {platform::KeyCode::w};
    snapshot.down_mouse_buttons = {platform::MouseButton::left};
    snapshot.pressed_mouse_buttons = {platform::MouseButton::left};
    snapshot.mouse_delta_x = 7;
    snapshot.mouse_delta_y = -3;
    auto gameplay = actions.evaluate(snapshot);
    assert(gameplay[input::InputAction::move_forward].held);
    assert(gameplay[input::InputAction::move_forward].pressed);
    assert(gameplay[input::InputAction::primary_action].pressed);
    assert(gameplay.look_delta_x == 7 && gameplay.look_delta_y == -3);

    actions.set_context(input::InputContext::inventory);
    auto inventory = actions.evaluate(snapshot);
    assert(!inventory[input::InputAction::move_forward].held);
    actions.set_context(input::InputContext::gameplay);
    assert(actions.rebind(input::InputBinding::keyboard(
        input::InputAction::move_forward, input::InputContext::gameplay, platform::KeyCode::r)));
    auto rebound = actions.evaluate(snapshot);
    assert(!rebound[input::InputAction::move_forward].held);
    snapshot.down_keys = {platform::KeyCode::r};
    assert(actions.evaluate(snapshot)[input::InputAction::move_forward].held);
    assert(!actions.bind(input::InputBinding::keyboard(
        input::InputAction::interact, input::InputContext::gameplay, platform::KeyCode::r)));
}

void test_voxel_dda_hits_faces_and_negative_coordinates() {
    world::ChunkDatabase chunks;
    auto& origin_chunk = chunks.get_or_create({0, 0, 0});
    assert(origin_chunk.set({2, 0, 2}, {1, 0, 0, 0}));
    auto downward = game::interaction::raycast_voxels(
        chunks, {world::WorldPosition{2.5, 2.5, 2.5}, {0.0, -1.0, 0.0}, 6.0});
    assert(downward && downward.value().hit.has_value());
    assert(downward.value().hit->block == world::BlockCoord(2, 0, 2));
    assert(downward.value().hit->adjacent_block == world::BlockCoord(2, 1, 2));
    assert(downward.value().hit->face_normal == math::Coord3i(0, 1, 0));
    assert(downward.value().hit->distance == 1.5);

    auto& negative_chunk = chunks.get_or_create({-1, 0, 0});
    assert(negative_chunk.set({31, 2, 2}, {1, 0, 0, 0}));
    auto negative = game::interaction::raycast_voxels(
        chunks, {world::WorldPosition{0.5, 2.5, 2.5}, {-1.0, 0.0, 0.0}, 2.0});
    assert(negative && negative.value().hit.has_value());
    assert(negative.value().hit->block == world::BlockCoord(-1, 2, 2));
    assert(negative.value().hit->face_normal == math::Coord3i(1, 0, 0));

    auto unloaded = game::interaction::raycast_voxels(
        chunks, {world::WorldPosition{31.5, 2.5, 2.5}, {1.0, 0.0, 0.0}, 4.0});
    assert(unloaded && !unloaded.value().hit.has_value());
    assert(unloaded.value().encountered_unloaded_chunk);
}

void test_voxel_dda_preserves_large_world_anchor() {
    constexpr std::int64_t anchor = 9'000'000'000'000'000LL;
    world::ChunkDatabase chunks;
    const world::BlockCoord block{anchor + 1, 4, anchor + 1};
    const auto address = world::block_to_chunk_local(block);
    auto& chunk = chunks.get_or_create(address.chunk);
    assert(chunk.set(address.local, {1, 0, 0, 0}));
    auto origin = world::WorldPosition::from_anchor({anchor, 4, anchor + 1}, {0.5, 0.5, 0.5});
    assert(origin);
    auto result = game::interaction::raycast_voxels(
        chunks, {origin.value(), {1.0, 0.0, 0.0}, 4.0});
    assert(result && result.value().hit.has_value());
    assert(result.value().hit->block == block);
    assert(result.value().hit->distance == 0.5);
}

} // namespace

int main() {
    test_contextual_action_map_and_rebinding();
    test_voxel_dda_hits_faces_and_negative_coordinates();
    test_voxel_dda_preserves_large_world_anchor();
    return 0;
}
