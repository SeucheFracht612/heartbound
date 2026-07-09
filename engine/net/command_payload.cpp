#include "engine/net/command_payload.hpp"

#include <string>
#include <utility>

namespace heartstead::net {

namespace {

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

[[nodiscard]] std::string percent_escape(std::string_view input) {
    std::string result;
    result.reserve(input.size());

    constexpr char hex[] = "0123456789ABCDEF";
    for (const auto value : input) {
        const auto byte = static_cast<unsigned char>(value);
        if (value == '%' || value == ';' || value == '=' || value == '|' || value == '\n' ||
            value == '\r') {
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
            return core::Result<std::string>::failure("command_payload.invalid_escape",
                                                      "command payload contains an invalid escape");
        }
        const auto high = hex_value(input[index + 1]);
        const auto low = hex_value(input[index + 2]);
        result.push_back(static_cast<char>((high << 4) | low));
        index += 2;
    }

    return core::Result<std::string>::success(std::move(result));
}

} // namespace

bool is_valid_command_payload_key(std::string_view key) noexcept {
    if (key.empty() || key.front() == '.' || key.back() == '.') {
        return false;
    }

    for (const auto character : key) {
        const auto valid = (character >= 'a' && character <= 'z') ||
                           (character >= '0' && character <= '9') || character == '_' ||
                           character == '-' || character == '.';
        if (!valid) {
            return false;
        }
    }
    return true;
}

core::Status CommandPayload::set(std::string key, std::string value) {
    if (!is_valid_command_payload_key(key)) {
        return core::Status::failure("command_payload.invalid_key",
                                     "command payload key is invalid: " + key);
    }

    auto [iterator, inserted] = fields_.emplace(std::move(key), std::move(value));
    if (!inserted) {
        return core::Status::failure("command_payload.duplicate_key",
                                     "command payload key is duplicated: " + iterator->first);
    }
    return core::Status::ok();
}

const std::string* CommandPayload::find(std::string_view key) const {
    const auto found = fields_.find(std::string(key));
    if (found == fields_.end()) {
        return nullptr;
    }
    return &found->second;
}

core::Result<std::string_view> CommandPayload::require(std::string_view key) const {
    const auto* value = find(key);
    if (value == nullptr || value->empty()) {
        return core::Result<std::string_view>::failure("command_payload.missing_required_key",
                                                       "command payload is missing required key: " +
                                                           std::string(key));
    }
    return core::Result<std::string_view>::success(*value);
}

const std::map<std::string, std::string>& CommandPayload::fields() const noexcept {
    return fields_;
}

std::size_t CommandPayload::size() const noexcept {
    return fields_.size();
}

std::string CommandPayloadTextCodec::encode(const CommandPayload& payload) {
    std::string result;
    bool first = true;
    for (const auto& [key, value] : payload.fields()) {
        if (!first) {
            result.push_back(';');
        }
        first = false;
        result.append(key);
        result.push_back('=');
        result.append(percent_escape(value));
    }
    return result;
}

core::Result<CommandPayload> CommandPayloadTextCodec::decode(std::string_view text) {
    if (text.empty()) {
        return core::Result<CommandPayload>::failure("command_payload.empty",
                                                     "command payload must not be empty");
    }

    CommandPayload payload;
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto end = text.find(';', start);
        const auto entry =
            end == std::string_view::npos ? text.substr(start) : text.substr(start, end - start);
        if (entry.empty()) {
            return core::Result<CommandPayload>::failure("command_payload.invalid_entry",
                                                         "command payload contains an empty entry");
        }
        if (entry.find('\n') != std::string_view::npos ||
            entry.find('\r') != std::string_view::npos) {
            return core::Result<CommandPayload>::failure(
                "command_payload.invalid_entry",
                "command payload entries must not contain raw line breaks");
        }

        const auto separator = entry.find('=');
        if (separator == std::string_view::npos || separator == 0) {
            return core::Result<CommandPayload>::failure(
                "command_payload.invalid_entry",
                "command payload entries must use key=value syntax");
        }

        const auto key = entry.substr(0, separator);
        const auto value = entry.substr(separator + 1);
        if (!is_valid_command_payload_key(key)) {
            return core::Result<CommandPayload>::failure("command_payload.invalid_key",
                                                         "command payload key is invalid: " +
                                                             std::string(key));
        }
        if (value.find('=') != std::string_view::npos) {
            return core::Result<CommandPayload>::failure(
                "command_payload.invalid_entry",
                "command payload values must escape reserved delimiters");
        }

        auto decoded_value = percent_unescape(value);
        if (!decoded_value) {
            return core::Result<CommandPayload>::failure(decoded_value.error().code,
                                                         decoded_value.error().message);
        }
        auto status = payload.set(std::string(key), std::move(decoded_value).value());
        if (!status) {
            return core::Result<CommandPayload>::failure(status.error().code,
                                                         status.error().message);
        }

        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }

    return core::Result<CommandPayload>::success(std::move(payload));
}

} // namespace heartstead::net
