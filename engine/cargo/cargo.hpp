#pragma once

#include "engine/core/ids.hpp"
#include "engine/core/result.hpp"
#include "engine/math/vector.hpp"

#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace heartstead::cargo {

using Vec3 = math::Vec3d;

enum class CargoTransportMode : std::uint32_t {
    hand = 1u << 0u,
    cart = 1u << 1u,
    wagon = 1u << 2u,
    boat = 1u << 3u,
    animal = 1u << 4u,
    crane = 1u << 5u,
};

class CargoTransportModes {
  public:
    CargoTransportModes() = default;

    static CargoTransportModes of(std::initializer_list<CargoTransportMode> modes);
    static CargoTransportModes from_bits(std::uint32_t bits) noexcept;

    void add(CargoTransportMode mode) noexcept;
    [[nodiscard]] bool allows(CargoTransportMode mode) const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] bool has_unknown_bits() const noexcept;
    [[nodiscard]] std::uint32_t bits() const noexcept;

  private:
    std::uint32_t bits_ = 0;
};

struct CargoRecord {
    core::SaveId cargo_id;
    core::PrototypeId prototype_id;
    Vec3 position;
    std::uint64_t mass_grams = 0;
    std::uint64_t volume_milliliters = 0;
    std::int32_t stability_per_mille = 1000;
    CargoTransportModes allowed_transport_modes;
    std::vector<std::string> hazard_tags;

    [[nodiscard]] core::Status validate() const;
    [[nodiscard]] bool is_hazardous() const noexcept;
};

struct CargoDefinition {
    core::PrototypeId prototype_id;
    std::uint64_t mass_grams = 0;
    std::uint64_t volume_milliliters = 0;
    std::int32_t stability_per_mille = 1000;
    CargoTransportModes allowed_transport_modes;
    std::vector<std::string> hazard_tags;

    [[nodiscard]] core::Status validate() const;
    [[nodiscard]] bool is_hazardous() const noexcept;
    [[nodiscard]] core::Result<CargoRecord> create_record(core::SaveId cargo_id,
                                                          Vec3 position = {}) const;
};

[[nodiscard]] core::Result<CargoTransportMode>
cargo_transport_mode_from_name(std::string_view name);
[[nodiscard]] std::string_view cargo_transport_mode_name(CargoTransportMode mode) noexcept;

} // namespace heartstead::cargo
