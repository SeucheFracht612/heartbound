#include "engine/core/ids.hpp"

#include <algorithm>
#include <cctype>

namespace heartstead::core {

namespace {

[[nodiscard]] bool is_lower_alnum(char c) noexcept {
    const auto value = static_cast<unsigned char>(c);
    return std::islower(value) != 0 || std::isdigit(value) != 0;
}

[[nodiscard]] bool is_namespace_char(char c) noexcept {
    return is_lower_alnum(c) || c == '_' || c == '-';
}

[[nodiscard]] bool is_local_char(char c) noexcept {
    return is_lower_alnum(c) || c == '_' || c == '-' || c == '/' || c == '.';
}

} // namespace

bool is_valid_namespace_id(std::string_view value) noexcept {
    if (value.empty() || !is_lower_alnum(value.front())) {
        return false;
    }

    return std::ranges::all_of(value, is_namespace_char);
}

bool is_valid_local_id(std::string_view value) noexcept {
    if (value.empty() || value.front() == '/' || value.back() == '/') {
        return false;
    }

    if (value.find("..") != std::string_view::npos || value.find("//") != std::string_view::npos) {
        return false;
    }

    return std::ranges::all_of(value, is_local_char);
}

std::optional<PrototypeId> PrototypeId::parse(std::string_view value) {
    const auto separator = value.find(':');
    if (separator == std::string_view::npos || separator == 0 || separator + 1 >= value.size()) {
        return std::nullopt;
    }

    const auto namespace_part = value.substr(0, separator);
    const auto local_part = value.substr(separator + 1);

    if (!is_valid_namespace_id(namespace_part) || !is_valid_local_id(local_part)) {
        return std::nullopt;
    }

    return PrototypeId(std::string(value));
}

std::string_view PrototypeId::namespace_id() const noexcept {
    const auto separator = value_.find(':');
    if (separator == std::string::npos) {
        return {};
    }
    return std::string_view(value_).substr(0, separator);
}

std::string_view PrototypeId::local_id() const noexcept {
    const auto separator = value_.find(':');
    if (separator == std::string::npos || separator + 1 >= value_.size()) {
        return {};
    }
    return std::string_view(value_).substr(separator + 1);
}

} // namespace heartstead::core
