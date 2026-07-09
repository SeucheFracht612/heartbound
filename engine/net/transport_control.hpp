#pragma once

#include "engine/core/ids.hpp"
#include "engine/core/result.hpp"
#include "engine/net/transport.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace heartstead::net {

inline constexpr std::uint32_t transport_control_protocol_version = 1;
inline constexpr std::string_view transport_server_welcome_payload_type = "control.server_welcome";
inline constexpr std::string_view transport_server_disconnect_payload_type =
    "control.server_disconnect";

struct TransportServerWelcome {
    std::uint32_t protocol_version = transport_control_protocol_version;
    core::NetId server_id;
    core::NetId assigned_client_id;
    std::uint32_t max_payload_bytes = 0;
    std::uint32_t max_clients = 0;
    bool supports_unreliable = false;
    bool enforces_reliable_command_order = false;
};

struct TransportClientSession {
    std::uint32_t protocol_version = transport_control_protocol_version;
    core::NetId server_id;
    core::NetId client_id;
    std::uint32_t max_payload_bytes = 0;
    std::uint32_t max_clients = 0;
    bool supports_unreliable = false;
    bool enforces_reliable_command_order = false;
    std::int64_t established_at_ms = 0;
};

struct TransportServerDisconnect {
    std::uint32_t protocol_version = transport_control_protocol_version;
    core::NetId server_id;
    core::NetId client_id;
    std::string reason_code = "server.disconnect";
    std::string reason_message;
};

class TransportControlTextCodec {
  public:
    [[nodiscard]] static std::string encode_server_welcome(const TransportServerWelcome& welcome);
    [[nodiscard]] static core::Result<TransportServerWelcome>
    decode_server_welcome(std::string_view text);
    [[nodiscard]] static std::string
    encode_server_disconnect(const TransportServerDisconnect& disconnect);
    [[nodiscard]] static core::Result<TransportServerDisconnect>
    decode_server_disconnect(std::string_view text);
};

[[nodiscard]] core::Status
validate_transport_server_welcome(const TransportServerWelcome& welcome) noexcept;
[[nodiscard]] core::Status
validate_transport_client_session(const TransportClientSession& session) noexcept;
[[nodiscard]] core::Result<TransportClientSession>
accept_transport_server_welcome(core::NetId expected_client_id, const TransportEnvelope& envelope);
[[nodiscard]] core::Status
validate_transport_server_disconnect(const TransportServerDisconnect& disconnect) noexcept;
[[nodiscard]] core::Result<TransportServerDisconnect>
accept_transport_server_disconnect(core::NetId expected_server_id, core::NetId expected_client_id,
                                   const TransportEnvelope& envelope);
[[nodiscard]] TransportMessage
make_server_welcome_transport_message(const TransportServerWelcome& welcome,
                                      std::int64_t server_time_ms);
[[nodiscard]] TransportMessage
make_server_disconnect_transport_message(const TransportServerDisconnect& disconnect,
                                         std::int64_t server_time_ms);

} // namespace heartstead::net
