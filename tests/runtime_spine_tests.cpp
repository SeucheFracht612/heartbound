#include "engine/content/content_validation.hpp"
#include "engine/net/command_payload.hpp"
#include "game/runtime/game_runtime.hpp"

#include <cassert>
#include <filesystem>
#include <string>

using namespace heartstead;

namespace {

struct TestFeatureComponent {
    std::uint32_t value = 0;
};

class ITestFeatureService {
  public:
    virtual ~ITestFeatureService() = default;
    [[nodiscard]] virtual std::uint32_t value() const noexcept = 0;
};

class TestFeatureService final : public ITestFeatureService {
  public:
    [[nodiscard]] std::uint32_t value() const noexcept override {
        return 42;
    }
};

class TestGameplayModule final : public game::IGameplayModule {
  public:
    [[nodiscard]] std::string_view module_id() const noexcept override {
        return "test.feature";
    }

    [[nodiscard]] core::Status
    register_components(game::ComponentRegistry& registry) override {
        return registry.register_component<TestFeatureComponent>("test.feature_component");
    }

    [[nodiscard]] core::Status
    register_services(game::DomainServiceRegistry& registry) override {
        return registry.register_service<ITestFeatureService>(
            "test.feature_service", std::make_shared<TestFeatureService>());
    }

    [[nodiscard]] core::Status
    register_commands(game::GameplayRegistrationContext& context) override {
        return context.commands.register_command({
            "test.feature.ping", false, true,
            [](const net::CommandEnvelope&, const net::CommandExecutionContext&,
               world::WorldOperation&) { return core::Status::ok(); },
        });
    }

    [[nodiscard]] core::Status
    register_systems(game::GameplayRegistrationContext& context) override {
        return context.scheduler.register_system({
            "test.feature.update", simulation::SimulationPhase::gameplay,
            {"runtime.physics"},
            [this](simulation::SimulationContext&) {
                ++update_count;
                return core::Status::ok();
            },
        });
    }

    [[nodiscard]] core::Status
    register_serializers(game::SerializationRegistry& registry) override {
        return registry.register_schema({"test.feature.state", 1});
    }

    [[nodiscard]] core::Status
    register_replication(game::ReplicationRegistry& registry) override {
        return registry.register_replication({"test.feature.delta", 1, true});
    }

    [[nodiscard]] core::Status
    register_presentation(game::PresentationRegistry& registry) override {
        return registry.register_adapter({"test.feature.presentation", 1});
    }

    std::uint32_t update_count = 0;
};

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
    assert(session->client()->local_player_net_id() == player_net_id);
    assert(session->client()->local_player_snapshot() != nullptr);
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
    auto render_snapshot = runtime.capture_render_snapshot();
    assert(render_snapshot);
    assert(render_snapshot.value().objects.size() == 1);
    assert(render_snapshot.value().objects.front().source_net_id == player_net_id);
    assert(render_snapshot.value().objects.front().current_transform.position ==
           player_after->state.position);
    assert(render_snapshot.value().objects.front().previous_transform.position == start);

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

void test_session_save_and_reload_restores_authoritative_state() {
    const auto report = content::ContentValidation::validate(source_root());
    assert(!report.has_errors());
    auto runtime = make_runtime(report);
    auto request = make_session_request(report);
    assert(runtime.start_session({}, request));
    auto* session = runtime.session();
    assert(session != nullptr && session->server() != nullptr && session->client() != nullptr);

    const auto clay = core::PrototypeId::parse("base:voxels/clay");
    assert(clay.has_value());
    const game::interaction::PlaceVoxelCommand placed_voxel{{10, 1, 8}, *clay};
    assert(session->submit_place_voxel(placed_voxel, 10));
    movement::PlayerInputFrame input;
    input.tick = 1;
    input.sequence = 1;
    input.move_z = 32'767;
    assert(session->submit_player_input(input, 10));
    auto frame = runtime.run_frame({16'667, 17});
    assert(frame && frame.value().server_ticks.size() == 1);
    const auto saved_player_position =
        session->server()->player_for_client(session->client()->client_id())->state.position;

    const auto save_root =
        std::filesystem::temp_directory_path() / "heartstead-runtime-save-reload-test";
    std::filesystem::remove_all(save_root);
    const save::FileSaveDatabase database(save_root);
    assert(runtime.save_to(database));
    auto persisted = database.read_snapshot();
    assert(persisted);
    assert(!persisted.value().chunk_edits.empty());
    assert(!persisted.value().entities.empty());
    assert(runtime.shutdown());

    assert(runtime.start_session_from_save({}, database, request.scenario_id));
    session = runtime.session();
    assert(session != nullptr && session->server() != nullptr && session->client() != nullptr);
    const auto address = world::block_to_chunk_local(placed_voxel.position);
    const auto authoritative =
        session->server()->world().chunks().get(address.chunk, address.local);
    const auto replicated = session->client()->world().chunks().get(address.chunk, address.local);
    assert(authoritative && replicated);
    assert(!authoritative.value().is_air());
    assert(replicated.value() == authoritative.value());
    const auto* restored_player =
        session->server()->player_for_client(session->client()->client_id());
    assert(restored_player != nullptr);
    assert(restored_player->state.position == saved_player_position);
    assert(session->client()->local_player_snapshot() != nullptr);
    assert(session->client()->local_player_snapshot()->state.position == saved_player_position);
    assert(runtime.shutdown());
    std::filesystem::remove_all(save_root);
}

void test_gameplay_modules_extend_runtime_through_registration_contract() {
    const auto report = content::ContentValidation::validate(source_root());
    assert(!report.has_errors());
    auto runtime = make_runtime(report);
    auto module = std::make_shared<TestGameplayModule>();
    game::RuntimeConfiguration config;
    config.gameplay_modules.push_back(module);
    assert(runtime.start_session(config, make_session_request(report)));
    const auto* server = runtime.session()->server();
    assert(server != nullptr);
    const auto& module_report = server->gameplay_modules().report();
    assert(module_report.module_ids.size() == 2);
    assert(module_report.module_ids.front() == "base.voxel_interaction");
    assert(module_report.module_ids.back() == "test.feature");
    assert(module_report.component_count == 1);
    assert(module_report.service_count == 1);
    assert(module_report.command_count == 3);
    assert(module_report.system_count == 1);
    assert(module_report.serializer_count == 3);
    assert(module_report.replication_count == 2);
    assert(module_report.presentation_adapter_count == 2);
    const auto* service = server->domain_services().find<ITestFeatureService>();
    assert(service != nullptr && service->value() == 42);
    assert(runtime.submit_command("test.feature.ping", {}, 10));
    auto frame = runtime.run_frame({16'667, 17});
    assert(frame && frame.value().server_ticks.size() == 1);
    assert(frame.value().server_ticks.front().commands.command_reports.size() == 1);
    assert(frame.value().server_ticks.front().commands.command_reports.front().success);
    assert(module->update_count == 1);
    assert(runtime.shutdown());
}

void test_replication_tombstone_removes_presentation_proxy() {
    const auto report = content::ContentValidation::validate(source_root());
    assert(!report.has_errors());
    auto runtime = make_runtime(report);
    assert(runtime.start_session({}, make_session_request(report)));
    auto* session = runtime.session();
    auto* server = session->server();
    assert(server != nullptr);
    auto second_client = server->connect_client();
    assert(second_client);
    const auto* second_player = server->player_for_client(second_client.value());
    assert(second_player != nullptr);
    const auto second_player_net_id = second_player->net_id;

    movement::PlayerInputFrame input;
    input.tick = 1;
    input.sequence = 1;
    input.move_z = 32'767;
    net::CommandEnvelope command;
    command.sequence = 1;
    command.sender = second_client.value();
    command.type = "player.input";
    command.payload = movement::PlayerInputTextCodec::encode(input);
    command.client_time_ms = 10;
    assert(server->submit_command(second_client.value(), std::move(command)));
    auto appeared = runtime.run_frame({16'667, 17});
    assert(appeared);
    auto snapshot = runtime.capture_render_snapshot();
    assert(snapshot && snapshot.value().objects.size() == 2);
    assert(session->presentation()->find_object(second_player_net_id) != nullptr);

    assert(server->disconnect_client(second_client.value()));
    auto disappeared = runtime.run_frame({16'667, 34});
    assert(disappeared);
    assert(disappeared.value().server_ticks.front().player_tombstone_count == 1);
    assert(disappeared.value().client.player_tombstone_count == 1);
    assert(disappeared.value().presentation.removed_objects == 1);
    assert(server->events().entity_destroyed.size() == 1);
    snapshot = runtime.capture_render_snapshot();
    assert(snapshot && snapshot.value().objects.size() == 1);
    assert(session->presentation()->find_object(second_player_net_id) == nullptr);
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
    test_session_save_and_reload_restores_authoritative_state();
    test_gameplay_modules_extend_runtime_through_registration_contract();
    test_replication_tombstone_removes_presentation_proxy();
    test_runtime_configuration_rejects_invalid_compositions();
    return 0;
}
