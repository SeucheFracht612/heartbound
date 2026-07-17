#include "engine/content/content_validation.hpp"
#include "engine/net/command_payload.hpp"
#include "game/runtime/game_runtime.hpp"

#include <cassert>
#include <filesystem>
#include <string>

using namespace heartstead;

namespace {

std::filesystem::path source_root() {
    return std::filesystem::path(HEARTSTEAD_TEST_SOURCE_DIR);
}

game::GameRuntime make_runtime(const content::ContentValidationReport& content_report) {
    auto runtime = game::GameRuntime::initialize(game::GameRuntimeConfig{}, content_report);
    assert(runtime);
    return std::move(runtime).value();
}

game::SessionRequest make_session_request(const content::ContentValidationReport& report) {
    auto metadata = content::save_metadata_from_content_report(report, "runtime-spine-test", 1234);
    assert(metadata);
    game::SessionRequest request;
    request.metadata = std::move(metadata).value();
    request.scenario_id = "base:scenarios/homestead";
    return request;
}

std::string set_voxel_payload() {
    net::CommandPayload payload;
    assert(payload.set("chunk", "0|0|0"));
    assert(payload.set("voxel", "1|2|3"));
    assert(payload.set("prototype", "base:voxels/clay"));
    return net::CommandPayloadTextCodec::encode(payload);
}

void test_local_runtime_advances_authority_through_loopback() {
    const auto report = content::ContentValidation::validate(source_root());
    assert(!report.has_errors());
    auto runtime = make_runtime(report);

    game::RuntimeConfiguration config;
    config.create_server = true;
    config.create_client = true;
    config.create_renderer = false;
    config.create_audio = false;
    config.use_in_memory_transport = true;
    config.headless = true;
    config.fixed_step = {60, 4, 250'000};
    assert(runtime.start_session(config, make_session_request(report)));
    assert(runtime.session() != nullptr);
    assert(runtime.session()->server() != nullptr);
    assert(runtime.session()->client() != nullptr);
    assert(runtime.session()->client()->is_connected());

    runtime.session()->server()->world().chunks().get_or_create({0, 0, 0}).clear_all_dirty();
    assert(runtime.submit_command("world.set_voxel", set_voxel_payload(), 10));

    auto frame = runtime.run_frame({16'667, 17});
    assert(frame);
    assert(frame.value().fixed_step.step_count == 1);
    assert(frame.value().server_ticks.size() == 1);
    assert(frame.value().server_ticks.front().commands.command_message_count == 1);
    assert(frame.value().server_ticks.front().commands.command_reports.size() == 1);
    assert(frame.value().server_ticks.front().commands.command_reports.front().success);
    assert(frame.value().client.command_result_count == 1);
    assert(frame.value().authoritative_world_tick == 1);
    assert(runtime.session()->server()->events().is_sealed());

    auto voxel = runtime.session()->server()->world().chunks().get({0, 0, 0}, {1, 2, 3});
    assert(voxel);
    const auto clay = report.voxel_palette.type_for(
        *core::PrototypeId::parse("base:voxels/clay"));
    assert(clay.has_value());
    assert(voxel.value().type == *clay);
    assert(runtime.session()->client()->command_results().size() == 1);
    assert(runtime.session()->client()->command_results().front().success);

    auto no_tick = runtime.run_frame({0, 17});
    assert(no_tick);
    assert(no_tick.value().fixed_step.step_count == 0);
    assert(no_tick.value().server_ticks.empty());
    assert(runtime.shutdown());
    assert(runtime.session() == nullptr);
}

void test_dedicated_headless_runtime_uses_same_scheduler() {
    const auto report = content::ContentValidation::validate(source_root());
    assert(!report.has_errors());
    auto runtime = make_runtime(report);
    game::RuntimeConfiguration config;
    config.create_server = true;
    config.create_client = false;
    config.headless = true;
    assert(runtime.start_session(config, make_session_request(report)));
    assert(runtime.session()->server() != nullptr);
    assert(runtime.session()->client() == nullptr);

    auto frame = runtime.run_frame({50'000, 50});
    assert(frame);
    assert(frame.value().fixed_step.step_count == 3);
    assert(frame.value().server_ticks.size() == 3);
    assert(frame.value().authoritative_world_tick == 3);
    const auto names = runtime.session()->server()->scheduler().ordered_system_names();
    assert(names.front() == "runtime.command_gateway");
    assert(names.back() == "runtime.replication");
    assert(runtime.shutdown());
}

void test_authoritative_player_input_moves_and_replicates() {
    const auto report = content::ContentValidation::validate(source_root());
    assert(!report.has_errors());
    auto runtime = make_runtime(report);
    game::RuntimeConfiguration config;
    config.fixed_step = {60, 4, 250'000};
    assert(runtime.start_session(config, make_session_request(report)));

    auto* session = runtime.session();
    assert(session != nullptr && session->server() != nullptr && session->client() != nullptr);
    auto initial_floor = session->client()->world().chunks().get({0, 0, 0}, {8, 0, 8});
    assert(initial_floor && !initial_floor.value().is_air());
    const auto client_id = session->client()->client_id();
    const auto* player_before = session->server()->player_for_client(client_id);
    assert(player_before != nullptr);
    const auto player_net_id = player_before->net_id;
    const auto start = player_before->state.position;

    movement::PlayerInputFrame input;
    input.tick = 1;
    input.sequence = 1;
    input.move_z = 32'767;
    assert(session->submit_player_input(input, 10));
    auto frame = runtime.run_frame({16'667, 17});
    assert(frame);
    assert(frame.value().server_ticks.size() == 1);
    const auto& tick = frame.value().server_ticks.front();
    assert(tick.commands.command_reports.size() == 1);
    assert(tick.commands.command_reports.front().success);
    assert(!tick.commands.command_reports.front().committed_world_mutation);
    assert(tick.moved_player_count == 1);
    assert(tick.movement_snapshot_count == 1);
    assert(session->server()->events().character_moved.size() == 1);

    const auto* player_after = session->server()->player_for_client(client_id);
    assert(player_after != nullptr);
    assert(player_after->state.position.relative_to(start.anchor).z > start.local_offset.z);
    const auto* snapshot = session->client()->player_snapshot(player_net_id);
    assert(snapshot != nullptr);
    assert(snapshot->state.position == player_after->state.position);
    assert(snapshot->last_processed_input_sequence == 1);

    auto repeated = runtime.run_frame({16'667, 34});
    assert(repeated && repeated.value().server_ticks.size() == 1);
    assert(repeated.value().server_ticks.front().repeated_input_count == 1);
    assert(repeated.value().server_ticks.front().movement_snapshot_count == 1);
    const auto* repeated_player = session->server()->player_for_client(client_id);
    assert(repeated_player != nullptr);
    assert(repeated_player->state.last_input_sequence == 1);
    assert(repeated_player->state.simulation_tick == 2);
    assert(runtime.shutdown());
}

void test_typed_voxel_commands_validate_and_replicate() {
    const auto report = content::ContentValidation::validate(source_root());
    assert(!report.has_errors());
    auto runtime = make_runtime(report);
    assert(runtime.start_session({}, make_session_request(report)));
    auto* session = runtime.session();
    assert(session != nullptr && session->server() != nullptr && session->client() != nullptr);
    const auto clay = core::PrototypeId::parse("base:voxels/clay");
    assert(clay.has_value());
    const game::interaction::PlaceVoxelCommand place{{9, 1, 8}, *clay};
    assert(session->submit_place_voxel(place, 10));
    auto placed = runtime.run_frame({16'667, 17});
    assert(placed && placed.value().server_ticks.size() == 1);
    assert(placed.value().server_ticks.front().commands.command_reports.front().success);
    assert(session->server()->events().voxel_changed.size() == 1);
    const auto address = world::block_to_chunk_local(place.position);
    auto authoritative = session->server()->world().chunks().get(address.chunk, address.local);
    auto replicated = session->client()->world().chunks().get(address.chunk, address.local);
    assert(authoritative && replicated);
    assert(!authoritative.value().is_air());
    assert(replicated.value() == authoritative.value());
    assert(placed.value().client.replication.total_applied_record_count == 1);

    assert(session->submit_place_voxel(place, 20));
    auto duplicate = runtime.run_frame({16'667, 34});
    assert(duplicate);
    assert(!duplicate.value().server_ticks.front().commands.command_reports.front().success);
    assert(duplicate.value().server_ticks.front().commands.command_reports.front().error_code ==
           "voxel_command.target_occupied");

    const game::interaction::PlaceVoxelCommand far_away{{100, 1, 100}, *clay};
    assert(session->submit_place_voxel(far_away, 30));
    auto rejected = runtime.run_frame({16'667, 51});
    assert(rejected);
    assert(!rejected.value().server_ticks.front().commands.command_reports.front().success);
    assert(rejected.value().server_ticks.front().commands.command_reports.front().error_code ==
           "voxel_command.out_of_reach");

    assert(session->submit_remove_voxel({place.position}, 40));
    auto removed = runtime.run_frame({16'667, 68});
    assert(removed);
    authoritative = session->server()->world().chunks().get(address.chunk, address.local);
    replicated = session->client()->world().chunks().get(address.chunk, address.local);
    assert(authoritative && authoritative.value().is_air());
    assert(replicated && replicated.value().is_air());
    assert(runtime.shutdown());
}

void test_runtime_configuration_rejects_invalid_compositions() {
    game::RuntimeConfiguration empty;
    empty.create_server = false;
    empty.create_client = false;
    assert(!empty.validate());

    game::RuntimeConfiguration loopback_client;
    loopback_client.create_server = false;
    loopback_client.create_client = true;
    loopback_client.use_in_memory_transport = true;
    assert(!loopback_client.validate());

    game::RuntimeConfiguration headless_renderer;
    headless_renderer.create_renderer = true;
    headless_renderer.headless = true;
    assert(!headless_renderer.validate());
}

} // namespace

int main() {
    test_local_runtime_advances_authority_through_loopback();
    test_dedicated_headless_runtime_uses_same_scheduler();
    test_authoritative_player_input_moves_and_replicates();
    test_typed_voxel_commands_validate_and_replicate();
    test_runtime_configuration_rejects_invalid_compositions();
    return 0;
}
