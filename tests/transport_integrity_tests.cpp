#include "engine/net/transport.hpp"
#include "engine/net/transport_reliability.hpp"

#include <cassert>
#include <cstdint>
#include <string>
#include <utility>

namespace {

using namespace heartstead;

[[nodiscard]] net::TransportMessage reliable_message(net::TransportMessageKind kind,
                                                     std::uint64_t sequence, std::string payload) {
    return {kind, net::TransportChannel::reliable, sequence, "test.reliable", std::move(payload),
            0};
}

void test_tracking_can_be_rolled_back_before_retry() {
    const auto client_id = core::NetId::from_value(2);
    const auto server_id = core::NetId::from_value(1);
    net::TransportReliabilityTracker tracker(client_id, server_id);
    const net::TransportEnvelope envelope{
        client_id, server_id, reliable_message(net::TransportMessageKind::command, 1, "first")};

    assert(tracker.track_send(envelope, 10));
    assert(tracker.pending_count() == 1);
    assert(tracker.rollback_tracked_send(envelope));
    assert(tracker.pending_count() == 0);
    assert(!tracker.rollback_tracked_send(envelope));
    assert(tracker.track_send(envelope, 20));
}

void test_external_capacity_failure_does_not_leak_untracked_datagram() {
    if (!net::transport_backend_info(net::TransportBackend::external_library).available) {
        return;
    }

    net::TransportHostDesc desc;
    desc.backend = net::TransportBackend::external_library;
    desc.external.server_id = core::NetId::from_value(1);
    desc.external.bind_endpoint = {"127.0.0.1", 0};
    desc.external.max_payload_bytes = 4096;
    desc.external.max_clients = 1;
    desc.external.reliability.max_in_flight = 1;
    auto transport = net::create_transport_host(desc);
    assert(transport);
    auto client = transport.value()->connect_client();
    assert(client);

    assert(transport.value()->send_client_to_server(
        client.value(), reliable_message(net::TransportMessageKind::command, 1, "first")));
    auto client_capacity = transport.value()->send_client_to_server(
        client.value(), reliable_message(net::TransportMessageKind::command, 2, "second"));
    assert(!client_capacity);
    assert(client_capacity.error().code == "transport_reliability.in_flight_limit_reached");

    auto server_messages = transport.value()->drain_server_messages();
    assert(server_messages.size() == 1);
    assert(server_messages.front().message.sequence == 1);
    auto client_messages = transport.value()->drain_client_messages(client.value());
    assert(client_messages && client_messages.value().empty());

    assert(transport.value()->send_client_to_server(
        client.value(), reliable_message(net::TransportMessageKind::command, 2, "second")));
    server_messages = transport.value()->drain_server_messages();
    assert(server_messages.size() == 1);
    assert(server_messages.front().message.sequence == 2);
    client_messages = transport.value()->drain_client_messages(client.value());
    assert(client_messages && client_messages.value().empty());

    assert(transport.value()->send_server_to_client(
        client.value(), reliable_message(net::TransportMessageKind::command_result, 1, "first")));
    auto server_capacity = transport.value()->send_server_to_client(
        client.value(), reliable_message(net::TransportMessageKind::command_result, 2, "second"));
    assert(!server_capacity);
    assert(server_capacity.error().code == "transport_reliability.in_flight_limit_reached");

    client_messages = transport.value()->drain_client_messages(client.value());
    assert(client_messages && client_messages.value().size() == 1);
    assert(client_messages.value().front().message.sequence == 1);
    server_messages = transport.value()->drain_server_messages();
    assert(server_messages.empty());

    assert(transport.value()->send_server_to_client(
        client.value(), reliable_message(net::TransportMessageKind::command_result, 2, "second")));
    client_messages = transport.value()->drain_client_messages(client.value());
    assert(client_messages && client_messages.value().size() == 1);
    assert(client_messages.value().front().message.sequence == 2);
}

} // namespace

int main() {
    test_tracking_can_be_rolled_back_before_retry();
    test_external_capacity_failure_does_not_leak_untracked_datagram();
    return 0;
}
