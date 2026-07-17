#include "engine/net/transport.hpp"

#include "engine/net/transport_packet.hpp"
#include "engine/net/transport_reliability.hpp"

#if defined(__unix__) || defined(__APPLE__)
#define HEARTSTEAD_HAS_POSIX_DATAGRAM_TRANSPORT 1
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#else
#define HEARTSTEAD_HAS_POSIX_DATAGRAM_TRANSPORT 0
#endif

#include <algorithm>
#include <array>
#include <optional>
#include <sstream>
#include <utility>

namespace heartstead::net {

namespace {

[[nodiscard]] bool is_valid_payload_type(std::string_view type) noexcept {
    if (type.empty() || type.front() == '.' || type.back() == '.') {
        return false;
    }

    for (const auto character : type) {
        const auto valid = (character >= 'a' && character <= 'z') ||
                           (character >= '0' && character <= '9') || character == '_' ||
                           character == '-' || character == '.';
        if (!valid) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool is_valid_endpoint_address(std::string_view address) noexcept {
    if (address.empty()) {
        return false;
    }

    for (const auto character : address) {
        if (character == '\n' || character == '\r' || character == '\t' || character == ' ') {
            return false;
        }
    }
    return true;
}

template <typename T> [[nodiscard]] std::vector<T> drain_queue(std::queue<T>& queue) {
    std::vector<T> result;
    result.reserve(queue.size());
    while (!queue.empty()) {
        result.push_back(std::move(queue.front()));
        queue.pop();
    }
    return result;
}

[[nodiscard]] bool is_reliability_ack_message(const TransportMessage& message) noexcept {
    return message.kind == TransportMessageKind::control &&
           message.payload_type == transport_reliability_ack_payload_type;
}

#if HEARTSTEAD_HAS_POSIX_DATAGRAM_TRANSPORT

[[nodiscard]] std::string socket_error_message(std::string_view action) {
    return std::string(action) + ": " + std::strerror(errno);
}

class PosixDatagramSocket final {
  public:
    PosixDatagramSocket() = default;
    explicit PosixDatagramSocket(int fd) noexcept : fd_(fd) {}

    ~PosixDatagramSocket() {
        close();
    }

    PosixDatagramSocket(const PosixDatagramSocket&) = delete;
    PosixDatagramSocket& operator=(const PosixDatagramSocket&) = delete;

    PosixDatagramSocket(PosixDatagramSocket&& other) noexcept : fd_(other.fd_) {
        other.fd_ = -1;
    }

    PosixDatagramSocket& operator=(PosixDatagramSocket&& other) noexcept {
        if (this != &other) {
            close();
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }

    [[nodiscard]] int fd() const noexcept {
        return fd_;
    }

    [[nodiscard]] bool is_valid() const noexcept {
        return fd_ >= 0;
    }

  private:
    void close() noexcept {
        if (fd_ >= 0) {
            (void)::close(fd_);
            fd_ = -1;
        }
    }

    int fd_ = -1;
};

[[nodiscard]] core::Result<PosixDatagramSocket> create_datagram_socket() {
    const auto fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return core::Result<PosixDatagramSocket>::failure(
            "transport.socket_failed", socket_error_message("failed to create UDP socket"));
    }

    PosixDatagramSocket socket(fd);
    const int enabled = 1;
    (void)::setsockopt(socket.fd(), SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));

    const auto flags = ::fcntl(socket.fd(), F_GETFL, 0);
    if (flags < 0 || ::fcntl(socket.fd(), F_SETFL, flags | O_NONBLOCK) < 0) {
        return core::Result<PosixDatagramSocket>::failure(
            "transport.socket_nonblocking_failed",
            socket_error_message("failed to configure UDP socket as nonblocking"));
    }
    return core::Result<PosixDatagramSocket>::success(std::move(socket));
}

[[nodiscard]] bool posix_datagram_transport_available() noexcept {
    const auto fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return false;
    }
    (void)::close(fd);
    return true;
}

[[nodiscard]] core::Result<sockaddr_in> ipv4_endpoint_address(const TransportEndpoint& endpoint) {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(endpoint.port);
    if (::inet_pton(AF_INET, endpoint.address.c_str(), &address.sin_addr) != 1) {
        return core::Result<sockaddr_in>::failure(
            "transport.invalid_endpoint_address",
            "external transport currently requires a numeric IPv4 bind address");
    }
    return core::Result<sockaddr_in>::success(address);
}

[[nodiscard]] sockaddr_in loopback_address(std::uint16_t port) noexcept {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    return address;
}

[[nodiscard]] core::Result<sockaddr_in> socket_bound_address(int fd) {
    sockaddr_in address{};
    socklen_t address_size = sizeof(address);
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&address), &address_size) != 0) {
        return core::Result<sockaddr_in>::failure(
            "transport.socket_address_failed",
            socket_error_message("failed to query UDP socket address"));
    }
    return core::Result<sockaddr_in>::success(address);
}

[[nodiscard]] std::uint16_t port_from_address(const sockaddr_in& address) noexcept {
    return ntohs(address.sin_port);
}

[[nodiscard]] bool same_socket_endpoint(const sockaddr_in& lhs, const sockaddr_in& rhs) noexcept {
    return lhs.sin_family == rhs.sin_family && lhs.sin_port == rhs.sin_port &&
           lhs.sin_addr.s_addr == rhs.sin_addr.s_addr;
}

[[nodiscard]] bool would_block() noexcept {
    return errno == EAGAIN || errno == EWOULDBLOCK;
}

[[nodiscard]] std::uint64_t fragment_source_scope(const sockaddr_in& address) noexcept {
    return (static_cast<std::uint64_t>(ntohl(address.sin_addr.s_addr)) << 16U) |
           static_cast<std::uint64_t>(ntohs(address.sin_port));
}

class PosixDatagramTransportHost final : public ITransportHost {
  private:
    struct ClientEndpoint {
        ClientEndpoint(PosixDatagramSocket socket_value, sockaddr_in address_value,
                       TransportPacketFragmentCodecConfig fragment_config, core::NetId server_id,
                       core::NetId client_id, TransportReliabilityConfig reliability_config)
            : socket(std::move(socket_value)), address(address_value), reassembler(fragment_config),
              server_reliability(server_id, client_id, reliability_config),
              client_reliability(client_id, server_id, reliability_config) {}

        PosixDatagramSocket socket;
        sockaddr_in address{};
        TransportPacketReassembler reassembler;
        TransportReliabilityTracker server_reliability;
        TransportReliabilityTracker client_reliability;
        std::uint64_t last_sent_reliable_command_sequence = 0;
        bool has_sent_reliable_command_sequence = false;
        std::uint64_t last_received_reliable_command_sequence = 0;
        bool has_received_reliable_command_sequence = false;
        bool connected = true;
    };

  public:
    [[nodiscard]] static core::Result<std::unique_ptr<ITransportHost>>
    create(ExternalTransportHostConfig config) {
        auto status = validate_external_transport_host_config(config);
        if (!status) {
            return core::Result<std::unique_ptr<ITransportHost>>::failure(status.error().code,
                                                                          status.error().message);
        }

        auto server_socket = create_datagram_socket();
        if (!server_socket) {
            return core::Result<std::unique_ptr<ITransportHost>>::failure(
                server_socket.error().code, server_socket.error().message);
        }
        auto bind_address = ipv4_endpoint_address(config.bind_endpoint);
        if (!bind_address) {
            return core::Result<std::unique_ptr<ITransportHost>>::failure(
                bind_address.error().code, bind_address.error().message);
        }
        if (::bind(server_socket.value().fd(),
                   reinterpret_cast<const sockaddr*>(&bind_address.value()),
                   sizeof(bind_address.value())) != 0) {
            return core::Result<std::unique_ptr<ITransportHost>>::failure(
                "transport.bind_failed",
                socket_error_message("failed to bind external transport endpoint " +
                                     transport_endpoint_name(config.bind_endpoint)));
        }

        auto actual_address = socket_bound_address(server_socket.value().fd());
        if (!actual_address) {
            return core::Result<std::unique_ptr<ITransportHost>>::failure(
                actual_address.error().code, actual_address.error().message);
        }

        auto host = std::unique_ptr<ITransportHost>(new PosixDatagramTransportHost(
            std::move(config), std::move(server_socket).value(), actual_address.value()));
        return core::Result<std::unique_ptr<ITransportHost>>::success(std::move(host));
    }

    [[nodiscard]] TransportBackend backend() const noexcept override {
        return TransportBackend::external_library;
    }

    [[nodiscard]] std::string_view backend_name() const noexcept override {
        return transport_backend_name(TransportBackend::external_library);
    }

    [[nodiscard]] TransportCapabilities capabilities() const noexcept override {
        return transport_host_capabilities(config_);
    }

    [[nodiscard]] core::NetId server_id() const noexcept override {
        return config_.server_id;
    }

    [[nodiscard]] std::size_t connected_client_count() const noexcept override {
        std::size_t count = 0;
        for (const auto& [_, client] : clients_) {
            if (client.connected) {
                ++count;
            }
        }
        return count;
    }

    [[nodiscard]] bool is_client_connected(core::NetId client_id) const noexcept override {
        const auto found = clients_.find(client_id.value());
        return found != clients_.end() && found->second.connected;
    }

    [[nodiscard]] std::vector<core::NetId> connected_client_ids() const override {
        std::vector<core::NetId> result;
        result.reserve(clients_.size());
        for (const auto& [id, client] : clients_) {
            if (client.connected) {
                result.push_back(core::NetId::from_value(id));
            }
        }
        std::ranges::sort(result);
        return result;
    }

    [[nodiscard]] core::Result<core::NetId> connect_client() override {
        if (connected_client_count() >= config_.max_clients) {
            return core::Result<core::NetId>::failure("transport.client_limit_reached",
                                                      "transport client limit has been reached");
        }

        auto client_socket = create_datagram_socket();
        if (!client_socket) {
            return core::Result<core::NetId>::failure(client_socket.error().code,
                                                      client_socket.error().message);
        }
        auto client_bind_address = loopback_address(0);
        if (::bind(client_socket.value().fd(),
                   reinterpret_cast<const sockaddr*>(&client_bind_address),
                   sizeof(client_bind_address)) != 0) {
            return core::Result<core::NetId>::failure(
                "transport.client_bind_failed",
                socket_error_message("failed to bind external transport loopback client"));
        }
        auto client_address = socket_bound_address(client_socket.value().fd());
        if (!client_address) {
            return core::Result<core::NetId>::failure(client_address.error().code,
                                                      client_address.error().message);
        }

        const auto id = next_client_id();
        ClientEndpoint endpoint{
            std::move(client_socket).value(),
            client_address.value(),
            fragment_config_,
            config_.server_id,
            id,
            config_.reliability,
        };
        clients_.emplace(id.value(), std::move(endpoint));
        return core::Result<core::NetId>::success(id);
    }

    [[nodiscard]] core::Status disconnect_client(core::NetId client_id) override {
        auto client = find_connected_client(client_id);
        if (!client) {
            return core::Status::failure(client.error().code, client.error().message);
        }
        server_reassembler_.discard_source(fragment_source_scope(client.value()->address));
        client.value()->reassembler.clear();
        client.value()->connected = false;
        return core::Status::ok();
    }

    [[nodiscard]] core::Status send_client_to_server(core::NetId client_id,
                                                     TransportMessage message) override {
        auto client = find_connected_client(client_id);
        if (!client) {
            return core::Status::failure(client.error().code, client.error().message);
        }
        auto status = validate_transport_message(message, config_.max_payload_bytes);
        if (!status) {
            return status;
        }
        status = validate_local_client_send_sequence(*client.value(), message);
        if (!status) {
            return status;
        }

        TransportEnvelope envelope{client_id, config_.server_id, std::move(message)};
        if (envelope.message.channel == TransportChannel::reliable) {
            status = client.value()->client_reliability.track_send(envelope, current_time_ms_);
            if (!status) {
                return status;
            }
        }
        auto sent =
            send_envelope(client.value()->socket.fd(), server_address_for_local_clients_, envelope);
        if (!sent) {
            if (envelope.message.channel == TransportChannel::reliable) {
                const auto rollback =
                    client.value()->client_reliability.rollback_tracked_send(envelope);
                if (!rollback) {
                    return core::Status::failure(
                        rollback.error().code,
                        sent.error().message +
                            "; tracking rollback failed: " + rollback.error().message);
                }
            }
            return sent;
        }
        record_local_client_send_sequence(*client.value(), envelope.message);
        return core::Status::ok();
    }

    [[nodiscard]] core::Status send_server_to_client(core::NetId client_id,
                                                     TransportMessage message) override {
        auto client = find_connected_client(client_id);
        if (!client) {
            return core::Status::failure(client.error().code, client.error().message);
        }
        auto status = validate_transport_message(message, config_.max_payload_bytes);
        if (!status) {
            return status;
        }
        TransportEnvelope envelope{config_.server_id, client_id, std::move(message)};
        if (envelope.message.channel == TransportChannel::reliable) {
            status = client.value()->server_reliability.track_send(envelope, current_time_ms_);
            if (!status) {
                return status;
            }
        }
        status = send_envelope(server_socket_.fd(), client.value()->address, envelope);
        if (!status) {
            if (envelope.message.channel == TransportChannel::reliable) {
                const auto rollback =
                    client.value()->server_reliability.rollback_tracked_send(envelope);
                if (!rollback) {
                    return core::Status::failure(
                        rollback.error().code,
                        status.error().message +
                            "; tracking rollback failed: " + rollback.error().message);
                }
            }
            return status;
        }
        return core::Status::ok();
    }

    [[nodiscard]] core::Result<TransportMaintenanceResult>
    poll_maintenance(std::int64_t now_ms) override {
        current_time_ms_ = now_ms;
        (void)server_reassembler_.expire(now_ms);
        TransportMaintenanceResult result;
        for (auto& [_, client] : clients_) {
            if (!client.connected) {
                continue;
            }
            (void)client.reassembler.expire(now_ms);

            auto server_poll = client.server_reliability.poll(now_ms);
            for (const auto& envelope : server_poll.retransmissions) {
                auto sent = send_envelope(server_socket_.fd(), client.address, envelope);
                if (!sent) {
                    return core::Result<TransportMaintenanceResult>::failure(sent.error().code,
                                                                             sent.error().message);
                }
                ++result.retransmission_count;
            }
            for (auto& dropped : server_poll.dropped) {
                result.dropped_reliable_messages.push_back(std::move(dropped.envelope));
                ++result.dropped_reliable_message_count;
            }

            auto client_poll = client.client_reliability.poll(now_ms);
            for (const auto& envelope : client_poll.retransmissions) {
                auto sent =
                    send_envelope(client.socket.fd(), server_address_for_local_clients_, envelope);
                if (!sent) {
                    return core::Result<TransportMaintenanceResult>::failure(sent.error().code,
                                                                             sent.error().message);
                }
                ++result.retransmission_count;
            }
            for (auto& dropped : client_poll.dropped) {
                result.dropped_reliable_messages.push_back(std::move(dropped.envelope));
                ++result.dropped_reliable_message_count;
            }
        }
        return core::Result<TransportMaintenanceResult>::success(std::move(result));
    }

    [[nodiscard]] std::vector<TransportEnvelope> drain_server_messages() override {
        std::vector<TransportEnvelope> result;
        drain_socket(
            server_socket_.fd(), server_reassembler_,
            [this, &result](TransportEnvelope envelope, const sockaddr_in& remote) {
                if (envelope.recipient != config_.server_id) {
                    return;
                }
                auto client = find_connected_client(envelope.sender);
                if (!client || !same_socket_endpoint(client.value()->address, remote)) {
                    return;
                }
                auto status =
                    validate_transport_message(envelope.message, config_.max_payload_bytes);
                if (!status) {
                    return;
                }
                if (is_reliability_ack_message(envelope.message)) {
                    (void)client.value()->server_reliability.accept_acknowledgement(envelope);
                    return;
                }
                if (envelope.message.channel == TransportChannel::reliable) {
                    auto reliable = client.value()->server_reliability.accept_reliable_message(
                        envelope, current_time_ms_);
                    if (!reliable) {
                        return;
                    }
                    (void)send_envelope(server_socket_.fd(), remote,
                                        reliable.value().acknowledgement);
                    if (reliable.value().duplicate) {
                        return;
                    }
                }
                status = validate_server_receive_sequence(*client.value(), envelope.message);
                if (!status) {
                    return;
                }
                record_server_receive_sequence(*client.value(), envelope.message);
                result.push_back(std::move(envelope));
            });
        return result;
    }

    [[nodiscard]] core::Result<std::vector<TransportEnvelope>>
    drain_client_messages(core::NetId client_id) override {
        auto client = find_client(client_id);
        if (!client) {
            return core::Result<std::vector<TransportEnvelope>>::failure(client.error().code,
                                                                         client.error().message);
        }

        std::vector<TransportEnvelope> result;
        auto* endpoint = client.value();
        drain_socket(endpoint->socket.fd(), endpoint->reassembler,
                     [this, client_id, endpoint, &result](TransportEnvelope envelope,
                                                          const sockaddr_in& remote) {
                         if (envelope.sender != config_.server_id ||
                             envelope.recipient != client_id ||
                             !same_socket_endpoint(remote, server_address_for_local_clients_)) {
                             return;
                         }
                         auto status = validate_transport_message(envelope.message,
                                                                  config_.max_payload_bytes);
                         if (!status) {
                             return;
                         }
                         if (is_reliability_ack_message(envelope.message)) {
                             (void)endpoint->client_reliability.accept_acknowledgement(envelope);
                             return;
                         }
                         if (envelope.message.channel == TransportChannel::reliable) {
                             auto reliable = endpoint->client_reliability.accept_reliable_message(
                                 envelope, current_time_ms_);
                             if (!reliable) {
                                 return;
                             }
                             (void)send_envelope(endpoint->socket.fd(), remote,
                                                 reliable.value().acknowledgement);
                             if (reliable.value().duplicate) {
                                 return;
                             }
                         }
                         result.push_back(std::move(envelope));
                     });
        return core::Result<std::vector<TransportEnvelope>>::success(std::move(result));
    }

  private:
    PosixDatagramTransportHost(ExternalTransportHostConfig config,
                               PosixDatagramSocket server_socket, sockaddr_in server_bound_address)
        : config_(std::move(config)), server_socket_(std::move(server_socket)),
          server_address_for_local_clients_(
              loopback_address(port_from_address(server_bound_address))),
          fragment_config_{datagram_payload_bytes, 1024u * 1024u, 1024},
          server_reassembler_(fragment_config_) {}

    [[nodiscard]] core::Result<ClientEndpoint*> find_client(core::NetId client_id) {
        if (!client_id.is_valid()) {
            return core::Result<ClientEndpoint*>::failure("transport.invalid_client_id",
                                                          "client net id must be valid");
        }
        const auto found = clients_.find(client_id.value());
        if (found == clients_.end()) {
            return core::Result<ClientEndpoint*>::failure(
                "transport.client_not_connected", "client is not known to this transport host");
        }
        return core::Result<ClientEndpoint*>::success(&found->second);
    }

    [[nodiscard]] core::Result<ClientEndpoint*> find_connected_client(core::NetId client_id) {
        auto client = find_client(client_id);
        if (!client) {
            return client;
        }
        if (!client.value()->connected) {
            return core::Result<ClientEndpoint*>::failure("transport.client_not_connected",
                                                          "client is not connected");
        }
        return client;
    }

    [[nodiscard]] core::NetId next_client_id() {
        const auto id = core::NetId::from_value(next_client_id_);
        ++next_client_id_;
        if (id == config_.server_id) {
            return next_client_id();
        }
        return id;
    }

    [[nodiscard]] core::Status
    validate_local_client_send_sequence(const ClientEndpoint& client,
                                        const TransportMessage& message) const {
        if (message.kind != TransportMessageKind::command ||
            message.channel != TransportChannel::reliable) {
            return core::Status::ok();
        }
        if (client.has_sent_reliable_command_sequence &&
            message.sequence <= client.last_sent_reliable_command_sequence) {
            return core::Status::failure(
                "transport.reliable_command_replayed",
                "reliable command sequence must be greater than the last sent command sequence");
        }
        return core::Status::ok();
    }

    void record_local_client_send_sequence(ClientEndpoint& client,
                                           const TransportMessage& message) const {
        if (message.kind == TransportMessageKind::command &&
            message.channel == TransportChannel::reliable) {
            client.last_sent_reliable_command_sequence = message.sequence;
            client.has_sent_reliable_command_sequence = true;
        }
    }

    [[nodiscard]] core::Status
    validate_server_receive_sequence(const ClientEndpoint& client,
                                     const TransportMessage& message) const {
        if (message.kind != TransportMessageKind::command ||
            message.channel != TransportChannel::reliable) {
            return core::Status::ok();
        }
        if (client.has_received_reliable_command_sequence &&
            message.sequence <= client.last_received_reliable_command_sequence) {
            return core::Status::failure("transport.reliable_command_replayed",
                                         "reliable command sequence must be greater than the last "
                                         "accepted command sequence");
        }
        return core::Status::ok();
    }

    void record_server_receive_sequence(ClientEndpoint& client,
                                        const TransportMessage& message) const {
        if (message.kind == TransportMessageKind::command &&
            message.channel == TransportChannel::reliable) {
            client.last_received_reliable_command_sequence = message.sequence;
            client.has_received_reliable_command_sequence = true;
        }
    }

    [[nodiscard]] core::Status send_envelope(int fd, const sockaddr_in& recipient,
                                             const TransportEnvelope& envelope) {
        const auto encoded_packet = TransportPacketCodec::encode(envelope);
        if (encoded_packet.size() <= datagram_payload_bytes) {
            return send_datagram(fd, recipient, encoded_packet);
        }

        auto fragments = TransportPacketFragmentCodec::fragment_packet(
            encoded_packet, next_packet_id_, fragment_config_);
        if (!fragments) {
            return core::Status::failure(fragments.error().code, fragments.error().message);
        }
        ++next_packet_id_;
        for (const auto& fragment : fragments.value()) {
            auto sent =
                send_datagram(fd, recipient, TransportPacketFragmentCodec::encode(fragment));
            if (!sent) {
                return sent;
            }
        }
        return core::Status::ok();
    }

    [[nodiscard]] core::Status send_datagram(int fd, const sockaddr_in& recipient,
                                             std::string_view datagram) const {
        const auto sent =
            ::sendto(fd, datagram.data(), datagram.size(), 0,
                     reinterpret_cast<const sockaddr*>(&recipient), sizeof(recipient));
        if (sent < 0 || static_cast<std::size_t>(sent) != datagram.size()) {
            return core::Status::failure(
                "transport.send_failed",
                socket_error_message("failed to send external transport datagram"));
        }
        return core::Status::ok();
    }

    template <typename Callback>
    void drain_socket(int fd, TransportPacketReassembler& reassembler, Callback&& callback) {
        std::array<char, max_datagram_bytes> buffer{};
        for (;;) {
            sockaddr_in remote{};
            socklen_t remote_size = sizeof(remote);
            const auto received = ::recvfrom(fd, buffer.data(), buffer.size(), 0,
                                             reinterpret_cast<sockaddr*>(&remote), &remote_size);
            if (received < 0) {
                if (would_block()) {
                    return;
                }
                return;
            }
            if (received == 0) {
                continue;
            }

            auto envelope =
                decode_datagram(std::string_view(buffer.data(), static_cast<std::size_t>(received)),
                                reassembler, fragment_source_scope(remote));
            if (envelope) {
                callback(std::move(envelope.value()), remote);
            }
        }
    }

    [[nodiscard]] std::optional<TransportEnvelope>
    decode_datagram(std::string_view datagram, TransportPacketReassembler& reassembler,
                    std::uint64_t source_scope) {
        auto packet = TransportPacketCodec::decode(
            datagram, TransportPacketCodecConfig{config_.max_payload_bytes});
        if (packet) {
            return std::move(packet).value();
        }

        auto fragment = TransportPacketFragmentCodec::decode(datagram, fragment_config_);
        if (!fragment) {
            return std::nullopt;
        }
        auto reassembled = reassembler.accept_fragment(source_scope, std::move(fragment).value(),
                                                       current_time_ms_);
        if (!reassembled || !reassembled.value().complete) {
            return std::nullopt;
        }
        auto decoded = TransportPacketCodec::decode(
            reassembled.value().packet, TransportPacketCodecConfig{config_.max_payload_bytes});
        if (!decoded) {
            return std::nullopt;
        }
        return std::move(decoded).value();
    }

    static constexpr std::uint32_t datagram_payload_bytes = 1200;
    static constexpr std::size_t max_datagram_bytes = 64U * 1024U;

    ExternalTransportHostConfig config_;
    PosixDatagramSocket server_socket_;
    sockaddr_in server_address_for_local_clients_{};
    TransportPacketFragmentCodecConfig fragment_config_{};
    TransportPacketReassembler server_reassembler_;
    std::uint64_t next_client_id_ = 2;
    std::uint64_t next_packet_id_ = 1;
    std::int64_t current_time_ms_ = 0;
    std::unordered_map<std::uint64_t, ClientEndpoint> clients_;
};

#endif

} // namespace

InMemoryTransportHost::InMemoryTransportHost(InMemoryTransportHostConfig config)
    : config_(config) {}

TransportBackend InMemoryTransportHost::backend() const noexcept {
    return TransportBackend::in_memory;
}

std::string_view InMemoryTransportHost::backend_name() const noexcept {
    return transport_backend_name(TransportBackend::in_memory);
}

TransportCapabilities InMemoryTransportHost::capabilities() const noexcept {
    return transport_host_capabilities(config_);
}

core::NetId InMemoryTransportHost::server_id() const noexcept {
    return config_.server_id;
}

std::size_t InMemoryTransportHost::connected_client_count() const noexcept {
    std::size_t count = 0;
    for (const auto& [_, client] : clients_) {
        if (client.connected) {
            ++count;
        }
    }
    return count;
}

bool InMemoryTransportHost::is_client_connected(core::NetId client_id) const noexcept {
    const auto found = clients_.find(client_id.value());
    return found != clients_.end() && found->second.connected;
}

std::vector<core::NetId> InMemoryTransportHost::connected_client_ids() const {
    std::vector<core::NetId> result;
    result.reserve(clients_.size());
    for (const auto& [id, client] : clients_) {
        if (client.connected) {
            result.push_back(core::NetId::from_value(id));
        }
    }
    std::ranges::sort(result);
    return result;
}

core::Result<core::NetId> InMemoryTransportHost::connect_client() {
    auto status = validate_transport_host_config(config_);
    if (!status) {
        return core::Result<core::NetId>::failure(status.error().code, status.error().message);
    }
    if (connected_client_count() >= config_.max_clients) {
        return core::Result<core::NetId>::failure("transport.client_limit_reached",
                                                  "transport client limit has been reached");
    }

    const auto id = next_client_id();
    clients_.emplace(id.value(), ClientQueues{});
    return core::Result<core::NetId>::success(id);
}

core::Status InMemoryTransportHost::disconnect_client(core::NetId client_id) {
    auto client = find_connected_client(client_id);
    if (!client) {
        return core::Status::failure(client.error().code, client.error().message);
    }
    client.value()->connected = false;
    return core::Status::ok();
}

core::Status InMemoryTransportHost::send_client_to_server(core::NetId client_id,
                                                          TransportMessage message) {
    auto client = find_connected_client(client_id);
    if (!client) {
        return core::Status::failure(client.error().code, client.error().message);
    }
    auto status = validate_message(message);
    if (!status) {
        return status;
    }
    status = validate_client_to_server_sequence(*client.value(), message);
    if (!status) {
        return status;
    }

    record_client_to_server_sequence(*client.value(), message);
    server_inbox_.push(TransportEnvelope{client_id, config_.server_id, std::move(message)});
    return core::Status::ok();
}

core::Status InMemoryTransportHost::send_server_to_client(core::NetId client_id,
                                                          TransportMessage message) {
    auto client = find_connected_client(client_id);
    if (!client) {
        return core::Status::failure(client.error().code, client.error().message);
    }
    auto status = validate_message(message);
    if (!status) {
        return status;
    }

    client.value()->inbox.push(TransportEnvelope{config_.server_id, client_id, std::move(message)});
    return core::Status::ok();
}

core::Result<TransportMaintenanceResult> InMemoryTransportHost::poll_maintenance(std::int64_t) {
    return core::Result<TransportMaintenanceResult>::success({});
}

std::vector<TransportEnvelope> InMemoryTransportHost::drain_server_messages() {
    return drain_queue(server_inbox_);
}

core::Result<std::vector<TransportEnvelope>>
InMemoryTransportHost::drain_client_messages(core::NetId client_id) {
    auto client = find_client(client_id);
    if (!client) {
        return core::Result<std::vector<TransportEnvelope>>::failure(client.error().code,
                                                                     client.error().message);
    }
    return core::Result<std::vector<TransportEnvelope>>::success(
        drain_queue(client.value()->inbox));
}

core::Status InMemoryTransportHost::validate_message(const TransportMessage& message) const {
    return validate_transport_message(message, config_.max_payload_bytes);
}

core::Status
InMemoryTransportHost::validate_client_to_server_sequence(const ClientQueues& client,
                                                          const TransportMessage& message) const {
    if (message.kind != TransportMessageKind::command ||
        message.channel != TransportChannel::reliable) {
        return core::Status::ok();
    }
    if (client.has_reliable_command_sequence &&
        message.sequence <= client.last_reliable_command_sequence) {
        return core::Status::failure(
            "transport.reliable_command_replayed",
            "reliable command sequence must be greater than the last accepted command sequence");
    }
    return core::Status::ok();
}

void InMemoryTransportHost::record_client_to_server_sequence(ClientQueues& client,
                                                             const TransportMessage& message) {
    if (message.kind == TransportMessageKind::command &&
        message.channel == TransportChannel::reliable) {
        client.last_reliable_command_sequence = message.sequence;
        client.has_reliable_command_sequence = true;
    }
}

core::Result<InMemoryTransportHost::ClientQueues*>
InMemoryTransportHost::find_client(core::NetId client_id) {
    if (!client_id.is_valid()) {
        return core::Result<ClientQueues*>::failure("transport.invalid_client_id",
                                                    "client net id must be valid");
    }
    const auto found = clients_.find(client_id.value());
    if (found == clients_.end()) {
        return core::Result<ClientQueues*>::failure("transport.client_not_connected",
                                                    "client is not known to this transport host");
    }
    return core::Result<ClientQueues*>::success(&found->second);
}

core::Result<InMemoryTransportHost::ClientQueues*>
InMemoryTransportHost::find_connected_client(core::NetId client_id) {
    auto client = find_client(client_id);
    if (!client) {
        return client;
    }
    if (!client.value()->connected) {
        return core::Result<ClientQueues*>::failure("transport.client_not_connected",
                                                    "client is not connected");
    }
    return client;
}

core::NetId InMemoryTransportHost::next_client_id() {
    const auto id = core::NetId::from_value(next_client_id_);
    ++next_client_id_;
    if (id == config_.server_id) {
        return next_client_id();
    }
    return id;
}

core::Result<std::unique_ptr<ITransportHost>> create_transport_host(TransportHostDesc desc) {
    auto status = validate_transport_host_desc(desc);
    if (!status) {
        return core::Result<std::unique_ptr<ITransportHost>>::failure(status.error().code,
                                                                      status.error().message);
    }

    switch (desc.backend) {
    case TransportBackend::in_memory:
        return core::Result<std::unique_ptr<ITransportHost>>::success(
            std::make_unique<InMemoryTransportHost>(desc.in_memory));
    case TransportBackend::external_library:
#if HEARTSTEAD_HAS_POSIX_DATAGRAM_TRANSPORT
        if (!posix_datagram_transport_available()) {
            return core::Result<std::unique_ptr<ITransportHost>>::failure(
                "transport.external_unavailable",
                "POSIX UDP socket transport is not available in this environment for " +
                    transport_endpoint_name(desc.external.bind_endpoint));
        }
        return PosixDatagramTransportHost::create(std::move(desc.external));
#else
        return core::Result<std::unique_ptr<ITransportHost>>::failure(
            "transport.external_unavailable",
            "external network transport is not compiled in yet for " +
                transport_endpoint_name(desc.external.bind_endpoint));
#endif
    }

    return core::Result<std::unique_ptr<ITransportHost>>::failure("transport.unknown_backend",
                                                                  "unknown transport backend");
}

core::Status validate_transport_host_desc(const TransportHostDesc& desc) {
    switch (desc.backend) {
    case TransportBackend::in_memory:
        return validate_transport_host_config(desc.in_memory);
    case TransportBackend::external_library:
        return validate_external_transport_host_config(desc.external);
    }
    return core::Status::failure("transport.unknown_backend", "unknown transport backend");
}

core::Status validate_transport_host_config(const InMemoryTransportHostConfig& config) {
    if (!config.server_id.is_valid()) {
        return core::Status::failure("transport.invalid_server_id", "server net id must be valid");
    }
    if (config.max_payload_bytes == 0) {
        return core::Status::failure("transport.invalid_max_payload",
                                     "max payload bytes must be non-zero");
    }
    if (config.max_clients == 0) {
        return core::Status::failure("transport.invalid_max_clients",
                                     "max client count must be non-zero");
    }
    return core::Status::ok();
}

core::Status validate_external_transport_host_config(const ExternalTransportHostConfig& config) {
    if (!config.server_id.is_valid()) {
        return core::Status::failure("transport.invalid_server_id", "server net id must be valid");
    }
    auto endpoint = validate_transport_endpoint(config.bind_endpoint);
    if (!endpoint) {
        return endpoint;
    }
    if (config.max_payload_bytes == 0) {
        return core::Status::failure("transport.invalid_max_payload",
                                     "max payload bytes must be non-zero");
    }
    if (config.max_clients == 0) {
        return core::Status::failure("transport.invalid_max_clients",
                                     "max client count must be non-zero");
    }
    auto reliability = validate_transport_reliability_config(config.reliability);
    if (!reliability) {
        return reliability;
    }
    return core::Status::ok();
}

core::Status validate_transport_endpoint(const TransportEndpoint& endpoint) {
    if (!is_valid_endpoint_address(endpoint.address)) {
        return core::Status::failure("transport.invalid_endpoint_address",
                                     "transport endpoint address must not be empty or contain "
                                     "whitespace");
    }
    return core::Status::ok();
}

core::Status validate_transport_message(const TransportMessage& message,
                                        std::uint32_t max_payload_bytes) {
    if (!is_valid_payload_type(message.payload_type)) {
        return core::Status::failure("transport.invalid_payload_type",
                                     "transport payload type must contain lowercase letters, "
                                     "digits, underscores, dashes, or dots");
    }
    if (message.payload.size() > max_payload_bytes) {
        return core::Status::failure("transport.payload_too_large",
                                     "transport payload exceeds max payload bytes");
    }
    return core::Status::ok();
}

core::Result<TransportCapabilities> transport_host_capabilities(const TransportHostDesc& desc) {
    auto status = validate_transport_host_desc(desc);
    if (!status) {
        return core::Result<TransportCapabilities>::failure(status.error().code,
                                                            status.error().message);
    }

    switch (desc.backend) {
    case TransportBackend::in_memory:
        return core::Result<TransportCapabilities>::success(
            transport_host_capabilities(desc.in_memory));
    case TransportBackend::external_library:
        return core::Result<TransportCapabilities>::success(
            transport_host_capabilities(desc.external));
    }
    return core::Result<TransportCapabilities>::failure("transport.unknown_backend",
                                                        "unknown transport backend");
}

TransportCapabilities
transport_host_capabilities(const InMemoryTransportHostConfig& config) noexcept {
    return TransportCapabilities{
        true, true, true, true, config.max_payload_bytes, config.max_clients,
    };
}

TransportCapabilities
transport_host_capabilities(const ExternalTransportHostConfig& config) noexcept {
    return TransportCapabilities{
        true, config.enable_unreliable_channel, true,
        true, config.max_payload_bytes,         config.max_clients,
    };
}

core::NetId transport_host_server_id(const TransportHostDesc& desc) noexcept {
    switch (desc.backend) {
    case TransportBackend::in_memory:
        return desc.in_memory.server_id;
    case TransportBackend::external_library:
        return desc.external.server_id;
    }
    return {};
}

TransportMessage make_command_transport_message(const CommandEnvelope& envelope,
                                                TransportChannel channel) {
    return TransportMessage{
        TransportMessageKind::command, channel, envelope.sequence, envelope.type, envelope.payload,
        envelope.client_time_ms};
}

core::Result<CommandEnvelope> command_envelope_from_transport(const TransportEnvelope& envelope) {
    if (envelope.message.kind != TransportMessageKind::command) {
        return core::Result<CommandEnvelope>::failure("transport.not_command_message",
                                                      "transport message is not a command");
    }
    if (!is_valid_payload_type(envelope.message.payload_type)) {
        return core::Result<CommandEnvelope>::failure("transport.invalid_payload_type",
                                                      "transport command type is invalid");
    }

    CommandEnvelope command;
    command.sequence = envelope.message.sequence;
    command.sender = envelope.sender;
    command.type = envelope.message.payload_type;
    command.payload = envelope.message.payload;
    command.client_time_ms = envelope.message.timestamp_ms;
    return core::Result<CommandEnvelope>::success(std::move(command));
}

TransportBackendInfo transport_backend_info(TransportBackend backend) noexcept {
    switch (backend) {
    case TransportBackend::in_memory:
        return TransportBackendInfo{
            TransportBackend::in_memory,
            transport_backend_name(TransportBackend::in_memory),
            true,
            "available",
        };
    case TransportBackend::external_library:
#if HEARTSTEAD_HAS_POSIX_DATAGRAM_TRANSPORT
        if (!posix_datagram_transport_available()) {
            return TransportBackendInfo{
                TransportBackend::external_library,
                transport_backend_name(TransportBackend::external_library),
                false,
                "POSIX UDP socket transport is blocked by the current environment",
            };
        }
        return TransportBackendInfo{
            TransportBackend::external_library,
            transport_backend_name(TransportBackend::external_library),
            true,
            "available through POSIX UDP packet transport",
        };
#else
        return TransportBackendInfo{
            TransportBackend::external_library,
            transport_backend_name(TransportBackend::external_library),
            false,
            "external network transport is not compiled in yet",
        };
#endif
    }
    return TransportBackendInfo{backend, "unknown", false, "unknown transport backend"};
}

std::string_view transport_backend_name(TransportBackend backend) noexcept {
    switch (backend) {
    case TransportBackend::in_memory:
        return "in_memory";
    case TransportBackend::external_library:
        return "external_library";
    }
    return "unknown";
}

std::string_view transport_channel_name(TransportChannel channel) noexcept {
    switch (channel) {
    case TransportChannel::reliable:
        return "reliable";
    case TransportChannel::unreliable:
        return "unreliable";
    }
    return "unknown";
}

std::string_view transport_message_kind_name(TransportMessageKind kind) noexcept {
    switch (kind) {
    case TransportMessageKind::command:
        return "command";
    case TransportMessageKind::command_result:
        return "command_result";
    case TransportMessageKind::replication:
        return "replication";
    case TransportMessageKind::control:
        return "control";
    }
    return "unknown";
}

std::string transport_endpoint_name(const TransportEndpoint& endpoint) {
    std::ostringstream output;
    output << endpoint.address << ':' << endpoint.port;
    return output.str();
}

} // namespace heartstead::net
