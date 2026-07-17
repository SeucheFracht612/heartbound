#pragma once

#include <compare>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace heartstead::core {

template <typename Tag> class GenerationalHandle {
  public:
    using index_type = std::uint32_t;
    using generation_type = std::uint32_t;

    constexpr GenerationalHandle() = default;

    [[nodiscard]] static constexpr GenerationalHandle from_parts(index_type index,
                                                                 generation_type generation) {
        return generation == 0 ? GenerationalHandle{} : GenerationalHandle(index, generation);
    }

    [[nodiscard]] static constexpr GenerationalHandle from_value(std::uint64_t value) {
        return from_parts(static_cast<index_type>(value & 0xFFFF'FFFFULL),
                          static_cast<generation_type>(value >> 32U));
    }

    [[nodiscard]] constexpr index_type index() const noexcept {
        return index_;
    }

    [[nodiscard]] constexpr generation_type generation() const noexcept {
        return generation_;
    }

    [[nodiscard]] constexpr std::uint64_t value() const noexcept {
        return (static_cast<std::uint64_t>(generation_) << 32U) |
               static_cast<std::uint64_t>(index_);
    }

    [[nodiscard]] constexpr bool is_valid() const noexcept {
        return generation_ != 0;
    }

    friend constexpr auto operator<=>(GenerationalHandle, GenerationalHandle) = default;

  private:
    constexpr GenerationalHandle(index_type index, generation_type generation)
        : index_(index), generation_(generation) {}

    index_type index_ = 0;
    generation_type generation_ = 0;
};

template <typename Tag> class StrongU64Id {
  public:
    using value_type = std::uint64_t;

    constexpr StrongU64Id() = default;

    static constexpr StrongU64Id from_value(value_type value) {
        return StrongU64Id(value);
    }

    [[nodiscard]] constexpr value_type value() const noexcept {
        return value_;
    }

    [[nodiscard]] constexpr bool is_valid() const noexcept {
        return value_ != 0;
    }

    [[nodiscard]] std::string to_string() const;

    friend constexpr auto operator<=>(StrongU64Id, StrongU64Id) = default;

  private:
    explicit constexpr StrongU64Id(value_type value) : value_(value) {}

    value_type value_ = 0;
};

struct SaveIdTag;
struct NetIdTag;
struct RuntimeHandleTag;
struct WorkpieceIdTag;
struct ProcessIdTag;

using SaveId = StrongU64Id<SaveIdTag>;
using NetId = StrongU64Id<NetIdTag>;
using RuntimeHandle = StrongU64Id<RuntimeHandleTag>;
using WorkpieceId = StrongU64Id<WorkpieceIdTag>;
using ProcessId = StrongU64Id<ProcessIdTag>;

[[nodiscard]] bool is_valid_namespace_id(std::string_view value) noexcept;
[[nodiscard]] bool is_valid_local_id(std::string_view value) noexcept;

class PrototypeId {
  public:
    [[nodiscard]] static std::optional<PrototypeId> parse(std::string_view value);

    PrototypeId() = default;

    [[nodiscard]] bool is_valid() const noexcept {
        return !value_.empty();
    }

    [[nodiscard]] const std::string& value() const noexcept {
        return value_;
    }

    [[nodiscard]] std::string_view namespace_id() const noexcept;
    [[nodiscard]] std::string_view local_id() const noexcept;

    friend auto operator<=>(const PrototypeId&, const PrototypeId&) = default;

  private:
    explicit PrototypeId(std::string value) : value_(std::move(value)) {}

    std::string value_;
};

template <typename Tag> std::string StrongU64Id<Tag>::to_string() const {
    return std::to_string(value_);
}

} // namespace heartstead::core
