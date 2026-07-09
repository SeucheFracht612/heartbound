#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace heartstead::core {

class StableHash64 {
  public:
    static constexpr std::uint64_t offset_basis = 1469598103934665603ull;
    static constexpr std::uint64_t prime = 1099511628211ull;

    void add_byte(std::uint8_t value) noexcept;
    void add_bytes(std::span<const std::uint8_t> bytes) noexcept;
    void add_string(std::string_view value) noexcept;
    void add_u64_le(std::uint64_t value) noexcept;

    [[nodiscard]] std::uint64_t value() const noexcept;
    [[nodiscard]] std::uint64_t nonzero_value() const noexcept;
    [[nodiscard]] std::string hex() const;

  private:
    std::uint64_t hash_ = offset_basis;
};

[[nodiscard]] std::string hex_u64(std::uint64_t value);
[[nodiscard]] std::uint64_t stable_hash64(std::string_view value) noexcept;
[[nodiscard]] std::uint64_t stable_hash64(std::span<const std::uint8_t> bytes) noexcept;
[[nodiscard]] std::string stable_hash64_hex(std::string_view value);
[[nodiscard]] std::string stable_hash64_hex(std::span<const std::uint8_t> bytes);

} // namespace heartstead::core
