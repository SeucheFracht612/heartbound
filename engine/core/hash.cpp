#include "engine/core/hash.hpp"

#include <iomanip>
#include <sstream>

namespace heartstead::core {

void StableHash64::add_byte(std::uint8_t value) noexcept {
    hash_ ^= value;
    hash_ *= prime;
}

void StableHash64::add_bytes(std::span<const std::uint8_t> bytes) noexcept {
    for (const auto value : bytes) {
        add_byte(value);
    }
}

void StableHash64::add_string(std::string_view value) noexcept {
    for (const auto character : value) {
        add_byte(static_cast<std::uint8_t>(character));
    }
}

void StableHash64::add_u64_le(std::uint64_t value) noexcept {
    for (std::uint32_t shift = 0; shift < 64; shift += 8) {
        add_byte(static_cast<std::uint8_t>((value >> shift) & 0xffU));
    }
}

std::uint64_t StableHash64::value() const noexcept {
    return hash_;
}

std::uint64_t StableHash64::nonzero_value() const noexcept {
    return hash_ == 0 ? 1 : hash_;
}

std::string StableHash64::hex() const {
    return hex_u64(hash_);
}

std::string hex_u64(std::uint64_t value) {
    std::ostringstream output;
    output << std::hex << std::setw(16) << std::setfill('0') << value;
    return output.str();
}

std::uint64_t stable_hash64(std::string_view value) noexcept {
    StableHash64 hasher;
    hasher.add_string(value);
    return hasher.value();
}

std::uint64_t stable_hash64(std::span<const std::uint8_t> bytes) noexcept {
    StableHash64 hasher;
    hasher.add_bytes(bytes);
    return hasher.value();
}

std::string stable_hash64_hex(std::string_view value) {
    return hex_u64(stable_hash64(value));
}

std::string stable_hash64_hex(std::span<const std::uint8_t> bytes) {
    return hex_u64(stable_hash64(bytes));
}

} // namespace heartstead::core
