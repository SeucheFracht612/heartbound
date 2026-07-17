#include "engine/core/logging.hpp"
#include "engine/core/process_entry.hpp"
#include "engine/net/client_session.hpp"
#include "engine/net/command_payload.hpp"
#include "engine/net/host_session.hpp"
#include "engine/net/replication.hpp"
#include "engine/net/server_command.hpp"
#include "engine/net/transport.hpp"
#include "engine/net/transport_control.hpp"
#include "engine/net/transport_packet.hpp"
#include "engine/world/world_commands.hpp"
#include "engine/world/world_state.hpp"

#include <string>

int main(int argc, char** argv) {
    return heartstead::core::run_no_argument_process_entry(argc, argv, [] {
        using namespace heartstead;

        core::log(core::LogLevel::info, "Heartstead network sandbox starting");

        net::TransportHostDesc external_desc;
        external_desc.backend = net::TransportBackend::external_library;
        external_desc.external = net::ExternalTransportHostConfig{
            core::NetId::from_value(1), net::TransportEndpoint{"127.0.0.1", 7777}, 4096, 8, true};

        const auto external_info =
            net::transport_backend_info(net::TransportBackend::external_library);
        core::log(core::LogLevel::info,
                  "External transport status: " + std::string(external_info.status) + " at " +
                      net::transport_endpoint_name(external_desc.external.bind_endpoint));
        auto external_capabilities = net::transport_host_capabilities(external_desc);
        if (!external_capabilities) {
            core::log(core::LogLevel::error, external_capabilities.error().message);
            return 1;
        }
        core::log(core::LogLevel::info,
                  "External transport contract: max clients " +
                      std::to_string(external_capabilities.value().max_clients) + ", max payload " +
                      std::to_string(external_capabilities.value().max_payload_bytes) +
                      ", unreliable " +
                      std::string(external_capabilities.value().supports_unreliable ? "enabled"
                                                                                    : "disabled") +
                      ", ordered reliable commands " +
                      std::string(external_capabilities.value().enforces_reliable_command_order
                                      ? "enforced"
                                      : "not_enforced"));

        auto local_transport = net::create_transport_host(net::TransportHostDesc{
            net::TransportBackend::in_memory,
            net::InMemoryTransportHostConfig{core::NetId::from_value(1), 4096, 8},
        });
        if (!local_transport) {
            core::log(core::LogLevel::error, local_transport.error().message);
            return 1;
        }
        core::log(core::LogLevel::info, "Selected transport backend: " +
                                            std::string(local_transport.value()->backend_name()));

        net::HostSession host(net::HostSessionConfig{
            net::TransportHostDesc{
                net::TransportBackend::in_memory,
                net::InMemoryTransportHostConfig{core::NetId::from_value(1), 4096, 8},
            },
            net::ReplicationRelevancePolicy{}});
        auto started = host.start();
        if (!started) {
            core::log(core::LogLevel::error, started.error().message);
            return 1;
        }

        auto client = host.connect_client();
        if (!client) {
            core::log(core::LogLevel::error, client.error().message);
            return 1;
        }
        auto welcome_messages = host.drain_client_messages(client.value());
        if (!welcome_messages || welcome_messages.value().size() != 1 ||
            welcome_messages.value().front().message.payload_type !=
                net::transport_server_welcome_payload_type) {
            core::log(core::LogLevel::error, "expected server welcome control message");
            return 1;
        }
        net::ClientSession client_session(client.value());
        auto client_welcome =
            client_session.receive_server_message(welcome_messages.value().front());
        if (!client_welcome) {
            core::log(core::LogLevel::error, "failed to accept server welcome control message: " +
                                                 client_welcome.error().message);
            return 1;
        }
        core::log(core::LogLevel::info,
                  "Server welcome assigned client " +
                      std::to_string(client_session.client_id().value()) + " with max payload " +
                      std::to_string(client_session.transport_session()->max_payload_bytes));

        world::WorldState world_state;
        world_state.chunks().get_or_create({0, 0, 0}).clear_all_dirty();
        net::ServerCommandDispatcher dispatcher;
        auto registered = world::WorldCommandRegistry::register_engine_commands(dispatcher);
        if (!registered) {
            core::log(core::LogLevel::error, registered.error().message);
            return 1;
        }

        net::CommandPayload payload;
        auto payload_status = payload.set("chunk", "0|0|0");
        if (payload_status) {
            payload_status = payload.set("voxel", "2|3|4");
        }
        if (payload_status) {
            payload_status = payload.set("prototype", "base:voxels/clay");
        }
        if (!payload_status) {
            core::log(core::LogLevel::error, payload_status.error().message);
            return 1;
        }
        auto outgoing = client_session.create_command(
            "world.set_voxel", net::CommandPayloadTextCodec::encode(payload), 100);
        if (!outgoing) {
            core::log(core::LogLevel::error, outgoing.error().message);
            return 1;
        }

        const auto outgoing_packet = net::TransportPacketCodec::encode(net::TransportEnvelope{
            outgoing.value().sender,
            host.server_id(),
            net::make_command_transport_message(outgoing.value()),
        });
        auto decoded_packet = net::TransportPacketCodec::decode(
            outgoing_packet, net::TransportPacketCodecConfig{1024});
        if (!decoded_packet) {
            core::log(core::LogLevel::error, decoded_packet.error().message);
            return 1;
        }
        auto decoded_command = net::command_envelope_from_transport(decoded_packet.value());
        if (!decoded_command) {
            core::log(core::LogLevel::error, decoded_command.error().message);
            return 1;
        }
        auto decoded_outgoing = decoded_command.value();

        auto sent = host.send_client_command(client.value(), decoded_outgoing);
        if (!sent) {
            core::log(core::LogLevel::error, sent.error().message);
            return 1;
        }

        net::CommandExecutionContext context;
        context.world_state = &world_state;
        world::VoxelPalette voxel_palette;
        world::VoxelDefinition clay;
        const auto clay_prototype = core::PrototypeId::parse("base:voxels/clay");
        if (!clay_prototype) {
            core::log(core::LogLevel::error, "sample clay prototype id is invalid");
            return 1;
        }
        clay.type = 7;
        clay.prototype_id = clay_prototype.value();
        clay.display_name = "Clay";
        clay.terrain_material = "clay";
        clay.mining_tool = "shovel";
        auto palette_status = voxel_palette.add(std::move(clay));
        if (!palette_status) {
            core::log(core::LogLevel::error, palette_status.error().message);
            return 1;
        }
        context.voxel_palette = &voxel_palette;
        auto tick = host.tick(dispatcher, context);
        if (!tick) {
            core::log(core::LogLevel::error, tick.error().message);
            return 1;
        }
        if (tick.value().command_reports.size() != 1 ||
            !tick.value().command_reports.front().success) {
            core::log(core::LogLevel::error, "host session command did not succeed");
            return 1;
        }

        auto edited = world_state.chunks().get({0, 0, 0}, {2, 3, 4});
        if (!edited || edited.value().type != 7) {
            core::log(core::LogLevel::error, "authoritative world did not apply voxel command");
            return 1;
        }

        auto client_messages = host.drain_client_messages(client.value());
        if (!client_messages || client_messages.value().size() != 2) {
            core::log(core::LogLevel::error,
                      "expected one command response and one replication message");
            return 1;
        }
        for (const auto& message : client_messages.value()) {
            auto received = client_session.receive_server_message(message);
            if (!received) {
                core::log(core::LogLevel::error, received.error().message);
                return 1;
            }
        }
        auto command_results = client_session.drain_command_results();
        auto replication_batches = client_session.drain_replication_batches();
        if (command_results.size() != 1 || !command_results.front().success ||
            !command_results.front().committed_world_mutation) {
            core::log(core::LogLevel::error, "failed to decode successful command response");
            return 1;
        }
        if (replication_batches.size() != 1 || replication_batches.front().events.empty()) {
            core::log(core::LogLevel::error, "failed to decode replication event batch");
            return 1;
        }

        core::log(
            core::LogLevel::info,
            "Command " + tick.value().command_reports.front().command_type + " sequence " +
                std::to_string(tick.value().command_reports.front().sequence) +
                " edited voxel type " + std::to_string(edited.value().type) + " and replicated " +
                std::to_string(replication_batches.front().events.size()) + " event(s)" +
                " after result event count " + std::to_string(command_results.front().event_count) +
                " through host session " + std::string(net::host_session_state_name(host.state())));
        return 0;
    });
}
