#include "engine/net/transport_control.hpp"

#include <charconv>
#include <cstdint>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace heartstead::net {

namespace {

constexpr std::string_view server_welcome_magic = "heartstead.transport_control.server_welcome.v1";
constexpr std::string_view server_disconnect_magic =
    "heartstead.transport_control.server_disconnect.v1";

[[nodiscard]] bool is_hex_digit(char value) noexcept {
    return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f') ||
           (value >= 'A' && value <= 'F');
}

[[nodiscard]] char hex_value(char value) noexcept {
    if (value >= '0' && value <= '9') {
        return static_cast<char>(value - '0');
    }
    if (value >= 'a' && value <= 'f') {
        return static_cast<char>(10 + value - 'a');
    }
    return static_cast<char>(10 + value - 'A');
}

[[nodiscard]] bool is_valid_control_code(std::string_view code) noexcept {
    if (code.empty() || code.front() == '.' || code.back() == '.') {
        return false;
    }
    for (const auto character : code) {
        const auto valid = (character >= 'a' && character <= 'z') ||
                           (character >= '0' && character <= '9') || character == '_' ||
                           character == '-' || character == '.';
        if (!valid) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::string percent_escape(std::string_view input) {
    std::string result;
    result.reserve(input.size());

    constexpr char hex[] = "0123456789ABCDEF";
    for (const auto value : input) {
        const auto byte = static_cast<unsigned char>(value);
        if (value == '%' || value == '=' || value == '\n' || value == '\r') {
            result.push_back('%');
            result.push_back(hex[(byte >> 4u) & 0x0Fu]);
            result.push_back(hex[byte & 0x0Fu]);
        } else {
            result.push_back(value);
        }
    }
    return result;
}

[[nodiscard]] core::Result<std::string> percent_unescape(std::string_view input) {
    std::string result;
    result.reserve(input.size());

    for (std::size_t index = 0; index < input.size(); ++index) {
        if (input[index] != '%') {
            result.push_back(input[index]);
            continue;
        }
        if (index + 2 >= input.size() || !is_hex_digit(input[index + 1]) ||
            !is_hex_digit(input[index + 2])) {
            return core::Result<std::string>::failure(
                "transport_control.invalid_escape",
                "transport control payload contains an invalid escape");
        }
        const auto high = hex_value(input[index + 1]);
        const auto low = hex_value(input[index + 2]);
        result.push_back(static_cast<char>((high << 4) | low));
        index += 2;
    }
    return core::Result<std::string>::success(std::move(result));
}

[[nodiscard]] core::Result<std::uint64_t> parse_u64(std::string_view value,
                                                    std::string_view field_name) {
    std::uint64_t parsed = 0;
    const auto* begin = value.data();
    const auto* end = value.data() + value.size();
    const auto [ptr, error] = std::from_chars(begin, end, parsed);
    if (error != std::errc{} || ptr != end) {
        return core::Result<std::uint64_t>::failure("transport_control.invalid_number",
                                                    "invalid numeric control field: " +
                                                        std::string(field_name));
    }
    return core::Result<std::uint64_t>::success(parsed);
}

[[nodiscard]] core::Result<std::uint32_t> parse_u32(std::string_view value,
                                                    std::string_view field_name) {
    auto parsed = parse_u64(value, field_name);
    if (!parsed) {
        return core::Result<std::uint32_t>::failure(parsed.error().code, parsed.error().message);
    }
    if (parsed.value() > static_cast<std::uint64_t>(UINT32_MAX)) {
        return core::Result<std::uint32_t>::failure("transport_control.number_out_of_range",
                                                    "numeric control field exceeds uint32 range: " +
                                                        std::string(field_name));
    }
    return core::Result<std::uint32_t>::success(static_cast<std::uint32_t>(parsed.value()));
}

[[nodiscard]] core::Result<bool> parse_bool(std::string_view value, std::string_view field_name) {
    if (value == "1") {
        return core::Result<bool>::success(true);
    }
    if (value == "0") {
        return core::Result<bool>::success(false);
    }
    return core::Result<bool>::failure("transport_control.invalid_bool",
                                       "invalid boolean control field: " + std::string(field_name));
}

[[nodiscard]] core::Result<std::map<std::string, std::string>>
parse_fields(std::string_view text, std::string_view expected_magic, std::string_view label) {
    std::map<std::string, std::string> fields;
    bool saw_magic = false;
    bool saw_end = false;

    std::size_t line_start = 0;
    while (line_start <= text.size()) {
        const auto line_end = text.find('\n', line_start);
        auto line = line_end == std::string_view::npos
                        ? text.substr(line_start)
                        : text.substr(line_start, line_end - line_start);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }

        if (!saw_magic) {
            if (line != expected_magic) {
                return core::Result<std::map<std::string, std::string>>::failure(
                    "transport_control.invalid_magic",
                    std::string(label) + " payload does not start with expected magic");
            }
            saw_magic = true;
        } else if (line == "end") {
            saw_end = true;
            break;
        } else if (!line.empty()) {
            const auto separator = line.find('=');
            if (separator == std::string_view::npos || separator == 0) {
                return core::Result<std::map<std::string, std::string>>::failure(
                    "transport_control.invalid_line",
                    std::string(label) + " payload line must use key=value syntax");
            }
            auto key = std::string(line.substr(0, separator));
            auto value = std::string(line.substr(separator + 1));
            const auto [_, inserted] = fields.emplace(std::move(key), std::move(value));
            if (!inserted) {
                return core::Result<std::map<std::string, std::string>>::failure(
                    "transport_control.duplicate_key",
                    std::string(label) + " payload contains a duplicate field");
            }
        }

        if (line_end == std::string_view::npos) {
            break;
        }
        line_start = line_end + 1;
    }

    if (!saw_magic) {
        return core::Result<std::map<std::string, std::string>>::failure(
            "transport_control.invalid_magic", std::string(label) + " payload is missing magic");
    }
    if (!saw_end) {
        return core::Result<std::map<std::string, std::string>>::failure(
            "transport_control.missing_end", std::string(label) + " payload is missing end marker");
    }
    return core::Result<std::map<std::string, std::string>>::success(std::move(fields));
}

[[nodiscard]] core::Result<std::string_view>
required_field(const std::map<std::string, std::string>& fields, std::string_view key,
               std::string_view label) {
    const auto found = fields.find(std::string(key));
    if (found == fields.end() || found->second.empty()) {
        return core::Result<std::string_view>::failure(
            "transport_control.missing_key",
            std::string(label) + " payload is missing required field: " + std::string(key));
    }
    return core::Result<std::string_view>::success(found->second);
}

template <typename T> [[nodiscard]] core::Result<T> fail_from(const core::Error& error) {
    return core::Result<T>::failure(error.code, error.message);
}

} // namespace

std::string
TransportControlTextCodec::encode_server_welcome(const TransportServerWelcome& welcome) {
    std::ostringstream output;
    output << server_welcome_magic << '\n';
    output << "protocol=" << welcome.protocol_version << '\n';
    output << "server_id=" << welcome.server_id.value() << '\n';
    output << "client_id=" << welcome.assigned_client_id.value() << '\n';
    output << "max_payload_bytes=" << welcome.max_payload_bytes << '\n';
    output << "max_clients=" << welcome.max_clients << '\n';
    output << "supports_unreliable=" << (welcome.supports_unreliable ? "1" : "0") << '\n';
    output << "reliable_order=" << (welcome.enforces_reliable_command_order ? "1" : "0") << '\n';
    output << "end\n";
    return output.str();
}

core::Result<TransportServerWelcome>
TransportControlTextCodec::decode_server_welcome(std::string_view text) {
    auto fields = parse_fields(text, server_welcome_magic, "server welcome");
    if (!fields) {
        return core::Result<TransportServerWelcome>::failure(fields.error().code,
                                                             fields.error().message);
    }

    auto protocol_value = required_field(fields.value(), "protocol", "server welcome");
    auto server_id_value = required_field(fields.value(), "server_id", "server welcome");
    auto client_id_value = required_field(fields.value(), "client_id", "server welcome");
    auto max_payload_value = required_field(fields.value(), "max_payload_bytes", "server welcome");
    auto max_clients_value = required_field(fields.value(), "max_clients", "server welcome");
    auto unreliable_value = required_field(fields.value(), "supports_unreliable", "server welcome");
    auto reliable_order_value = required_field(fields.value(), "reliable_order", "server welcome");
    if (!protocol_value || !server_id_value || !client_id_value || !max_payload_value ||
        !max_clients_value || !unreliable_value || !reliable_order_value) {
        return core::Result<TransportServerWelcome>::failure(
            "transport_control.missing_key", "server welcome payload is missing required fields");
    }

    auto protocol = parse_u32(protocol_value.value(), "protocol");
    auto server_id = parse_u64(server_id_value.value(), "server_id");
    auto client_id = parse_u64(client_id_value.value(), "client_id");
    auto max_payload = parse_u32(max_payload_value.value(), "max_payload_bytes");
    auto max_clients = parse_u32(max_clients_value.value(), "max_clients");
    auto supports_unreliable = parse_bool(unreliable_value.value(), "supports_unreliable");
    auto reliable_order = parse_bool(reliable_order_value.value(), "reliable_order");
    if (!protocol) {
        return fail_from<TransportServerWelcome>(protocol.error());
    }
    if (!server_id) {
        return fail_from<TransportServerWelcome>(server_id.error());
    }
    if (!client_id) {
        return fail_from<TransportServerWelcome>(client_id.error());
    }
    if (!max_payload) {
        return fail_from<TransportServerWelcome>(max_payload.error());
    }
    if (!max_clients) {
        return fail_from<TransportServerWelcome>(max_clients.error());
    }
    if (!supports_unreliable) {
        return fail_from<TransportServerWelcome>(supports_unreliable.error());
    }
    if (!reliable_order) {
        return fail_from<TransportServerWelcome>(reliable_order.error());
    }

    TransportServerWelcome welcome;
    welcome.protocol_version = protocol.value();
    welcome.server_id = core::NetId::from_value(server_id.value());
    welcome.assigned_client_id = core::NetId::from_value(client_id.value());
    welcome.max_payload_bytes = max_payload.value();
    welcome.max_clients = max_clients.value();
    welcome.supports_unreliable = supports_unreliable.value();
    welcome.enforces_reliable_command_order = reliable_order.value();

    auto status = validate_transport_server_welcome(welcome);
    if (!status) {
        return core::Result<TransportServerWelcome>::failure(status.error().code,
                                                             status.error().message);
    }
    return core::Result<TransportServerWelcome>::success(welcome);
}

std::string
TransportControlTextCodec::encode_server_disconnect(const TransportServerDisconnect& disconnect) {
    std::ostringstream output;
    output << server_disconnect_magic << '\n';
    output << "protocol=" << disconnect.protocol_version << '\n';
    output << "server_id=" << disconnect.server_id.value() << '\n';
    output << "client_id=" << disconnect.client_id.value() << '\n';
    output << "reason_code=" << disconnect.reason_code << '\n';
    output << "message=" << percent_escape(disconnect.reason_message) << '\n';
    output << "end\n";
    return output.str();
}

core::Result<TransportServerDisconnect>
TransportControlTextCodec::decode_server_disconnect(std::string_view text) {
    auto fields = parse_fields(text, server_disconnect_magic, "server disconnect");
    if (!fields) {
        return core::Result<TransportServerDisconnect>::failure(fields.error().code,
                                                                fields.error().message);
    }

    auto protocol_value = required_field(fields.value(), "protocol", "server disconnect");
    auto server_id_value = required_field(fields.value(), "server_id", "server disconnect");
    auto client_id_value = required_field(fields.value(), "client_id", "server disconnect");
    auto reason_code_value = required_field(fields.value(), "reason_code", "server disconnect");
    const auto message_value = fields.value().find("message");
    if (!protocol_value || !server_id_value || !client_id_value || !reason_code_value ||
        message_value == fields.value().end()) {
        return core::Result<TransportServerDisconnect>::failure(
            "transport_control.missing_key",
            "server disconnect payload is missing required fields");
    }

    auto protocol = parse_u32(protocol_value.value(), "protocol");
    auto server_id = parse_u64(server_id_value.value(), "server_id");
    auto client_id = parse_u64(client_id_value.value(), "client_id");
    auto message = percent_unescape(message_value->second);
    if (!protocol) {
        return fail_from<TransportServerDisconnect>(protocol.error());
    }
    if (!server_id) {
        return fail_from<TransportServerDisconnect>(server_id.error());
    }
    if (!client_id) {
        return fail_from<TransportServerDisconnect>(client_id.error());
    }
    if (!message) {
        return fail_from<TransportServerDisconnect>(message.error());
    }

    TransportServerDisconnect disconnect;
    disconnect.protocol_version = protocol.value();
    disconnect.server_id = core::NetId::from_value(server_id.value());
    disconnect.client_id = core::NetId::from_value(client_id.value());
    disconnect.reason_code = std::string(reason_code_value.value());
    disconnect.reason_message = std::move(message).value();

    auto status = validate_transport_server_disconnect(disconnect);
    if (!status) {
        return core::Result<TransportServerDisconnect>::failure(status.error().code,
                                                                status.error().message);
    }
    return core::Result<TransportServerDisconnect>::success(disconnect);
}

core::Status validate_transport_server_welcome(const TransportServerWelcome& welcome) noexcept {
    if (welcome.protocol_version != transport_control_protocol_version) {
        return core::Status::failure("transport_control.unsupported_protocol_version",
                                     "server welcome protocol version is unsupported");
    }
    if (!welcome.server_id.is_valid()) {
        return core::Status::failure("transport_control.invalid_server_id",
                                     "server welcome server id must be valid");
    }
    if (!welcome.assigned_client_id.is_valid()) {
        return core::Status::failure("transport_control.invalid_client_id",
                                     "server welcome client id must be valid");
    }
    if (welcome.server_id == welcome.assigned_client_id) {
        return core::Status::failure("transport_control.id_collision",
                                     "server welcome server id and client id must differ");
    }
    if (welcome.max_payload_bytes == 0) {
        return core::Status::failure("transport_control.invalid_max_payload",
                                     "server welcome max payload bytes must be non-zero");
    }
    if (welcome.max_clients == 0) {
        return core::Status::failure("transport_control.invalid_max_clients",
                                     "server welcome max client count must be non-zero");
    }
    return core::Status::ok();
}

core::Status validate_transport_client_session(const TransportClientSession& session) noexcept {
    if (session.protocol_version != transport_control_protocol_version) {
        return core::Status::failure("transport_control.unsupported_protocol_version",
                                     "client session protocol version is unsupported");
    }
    if (!session.server_id.is_valid()) {
        return core::Status::failure("transport_control.invalid_server_id",
                                     "client session server id must be valid");
    }
    if (!session.client_id.is_valid()) {
        return core::Status::failure("transport_control.invalid_client_id",
                                     "client session client id must be valid");
    }
    if (session.server_id == session.client_id) {
        return core::Status::failure("transport_control.id_collision",
                                     "client session server id and client id must differ");
    }
    if (session.max_payload_bytes == 0) {
        return core::Status::failure("transport_control.invalid_max_payload",
                                     "client session max payload bytes must be non-zero");
    }
    if (session.max_clients == 0) {
        return core::Status::failure("transport_control.invalid_max_clients",
                                     "client session max client count must be non-zero");
    }
    return core::Status::ok();
}

core::Result<TransportClientSession>
accept_transport_server_welcome(core::NetId expected_client_id, const TransportEnvelope& envelope) {
    if (!expected_client_id.is_valid()) {
        return core::Result<TransportClientSession>::failure(
            "transport_control.invalid_expected_client_id",
            "expected client id must be valid before accepting server welcome");
    }
    if (envelope.message.kind != TransportMessageKind::control) {
        return core::Result<TransportClientSession>::failure(
            "transport_control.unexpected_message_kind",
            "server welcome must arrive as a control transport message");
    }
    if (envelope.message.channel != TransportChannel::reliable) {
        return core::Result<TransportClientSession>::failure(
            "transport_control.unreliable_server_welcome",
            "server welcome must arrive on the reliable transport channel");
    }
    if (envelope.message.payload_type != transport_server_welcome_payload_type) {
        return core::Result<TransportClientSession>::failure(
            "transport_control.unexpected_payload_type",
            "server welcome transport message has an unexpected payload type");
    }
    if (envelope.recipient != expected_client_id) {
        return core::Result<TransportClientSession>::failure(
            "transport_control.recipient_mismatch",
            "server welcome recipient does not match the expected client id");
    }

    auto welcome = TransportControlTextCodec::decode_server_welcome(envelope.message.payload);
    if (!welcome) {
        return core::Result<TransportClientSession>::failure(welcome.error().code,
                                                             welcome.error().message);
    }
    if (welcome.value().assigned_client_id != expected_client_id) {
        return core::Result<TransportClientSession>::failure(
            "transport_control.client_id_mismatch",
            "server welcome assigned client id does not match the expected client id");
    }
    if (welcome.value().server_id != envelope.sender) {
        return core::Result<TransportClientSession>::failure(
            "transport_control.server_id_mismatch",
            "server welcome server id does not match the transport envelope sender");
    }

    TransportClientSession session;
    session.protocol_version = welcome.value().protocol_version;
    session.server_id = welcome.value().server_id;
    session.client_id = welcome.value().assigned_client_id;
    session.max_payload_bytes = welcome.value().max_payload_bytes;
    session.max_clients = welcome.value().max_clients;
    session.supports_unreliable = welcome.value().supports_unreliable;
    session.enforces_reliable_command_order = welcome.value().enforces_reliable_command_order;
    session.established_at_ms = envelope.message.timestamp_ms;

    auto status = validate_transport_client_session(session);
    if (!status) {
        return core::Result<TransportClientSession>::failure(status.error().code,
                                                             status.error().message);
    }
    return core::Result<TransportClientSession>::success(session);
}

core::Status
validate_transport_server_disconnect(const TransportServerDisconnect& disconnect) noexcept {
    if (disconnect.protocol_version != transport_control_protocol_version) {
        return core::Status::failure("transport_control.unsupported_protocol_version",
                                     "server disconnect protocol version is unsupported");
    }
    if (!disconnect.server_id.is_valid()) {
        return core::Status::failure("transport_control.invalid_server_id",
                                     "server disconnect server id must be valid");
    }
    if (!disconnect.client_id.is_valid()) {
        return core::Status::failure("transport_control.invalid_client_id",
                                     "server disconnect client id must be valid");
    }
    if (disconnect.server_id == disconnect.client_id) {
        return core::Status::failure("transport_control.id_collision",
                                     "server disconnect server id and client id must differ");
    }
    if (!is_valid_control_code(disconnect.reason_code)) {
        return core::Status::failure(
            "transport_control.invalid_reason_code",
            "server disconnect reason code must contain lowercase letters, digits, "
            "underscores, dashes, or dots");
    }
    return core::Status::ok();
}

core::Result<TransportServerDisconnect>
accept_transport_server_disconnect(core::NetId expected_server_id, core::NetId expected_client_id,
                                   const TransportEnvelope& envelope) {
    if (!expected_server_id.is_valid()) {
        return core::Result<TransportServerDisconnect>::failure(
            "transport_control.invalid_expected_server_id",
            "expected server id must be valid before accepting server disconnect");
    }
    if (!expected_client_id.is_valid()) {
        return core::Result<TransportServerDisconnect>::failure(
            "transport_control.invalid_expected_client_id",
            "expected client id must be valid before accepting server disconnect");
    }
    if (envelope.message.kind != TransportMessageKind::control) {
        return core::Result<TransportServerDisconnect>::failure(
            "transport_control.unexpected_message_kind",
            "server disconnect must arrive as a control transport message");
    }
    if (envelope.message.channel != TransportChannel::reliable) {
        return core::Result<TransportServerDisconnect>::failure(
            "transport_control.unreliable_server_disconnect",
            "server disconnect must arrive on the reliable transport channel");
    }
    if (envelope.message.payload_type != transport_server_disconnect_payload_type) {
        return core::Result<TransportServerDisconnect>::failure(
            "transport_control.unexpected_payload_type",
            "server disconnect transport message has an unexpected payload type");
    }
    if (envelope.sender != expected_server_id) {
        return core::Result<TransportServerDisconnect>::failure(
            "transport_control.server_id_mismatch",
            "server disconnect sender does not match the expected server id");
    }
    if (envelope.recipient != expected_client_id) {
        return core::Result<TransportServerDisconnect>::failure(
            "transport_control.recipient_mismatch",
            "server disconnect recipient does not match the expected client id");
    }

    auto disconnect = TransportControlTextCodec::decode_server_disconnect(envelope.message.payload);
    if (!disconnect) {
        return disconnect;
    }
    if (disconnect.value().server_id != expected_server_id) {
        return core::Result<TransportServerDisconnect>::failure(
            "transport_control.server_id_mismatch",
            "server disconnect server id does not match the transport envelope sender");
    }
    if (disconnect.value().client_id != expected_client_id) {
        return core::Result<TransportServerDisconnect>::failure(
            "transport_control.client_id_mismatch",
            "server disconnect client id does not match the expected client id");
    }
    return disconnect;
}

TransportMessage make_server_welcome_transport_message(const TransportServerWelcome& welcome,
                                                       std::int64_t server_time_ms) {
    return TransportMessage{
        TransportMessageKind::control,
        TransportChannel::reliable,
        0,
        std::string(transport_server_welcome_payload_type),
        TransportControlTextCodec::encode_server_welcome(welcome),
        server_time_ms,
    };
}

TransportMessage
make_server_disconnect_transport_message(const TransportServerDisconnect& disconnect,
                                         std::int64_t server_time_ms) {
    return TransportMessage{
        TransportMessageKind::control,
        TransportChannel::reliable,
        0,
        std::string(transport_server_disconnect_payload_type),
        TransportControlTextCodec::encode_server_disconnect(disconnect),
        server_time_ms,
    };
}

} // namespace heartstead::net
