#include "engine/cargo/cargo.hpp"

#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace heartstead::cargo {

namespace {

[[nodiscard]] constexpr std::uint32_t bit(CargoTransportMode mode) noexcept {
    return static_cast<std::uint32_t>(mode);
}

[[nodiscard]] constexpr std::uint32_t known_transport_mode_bits() noexcept {
    return bit(CargoTransportMode::hand) | bit(CargoTransportMode::cart) |
           bit(CargoTransportMode::wagon) | bit(CargoTransportMode::boat) |
           bit(CargoTransportMode::animal) | bit(CargoTransportMode::crane);
}

[[nodiscard]] core::Status validate_hazard_tags(const std::vector<std::string>& tags,
                                                std::string_view error_prefix,
                                                std::string_view label) {
    std::set<std::string> seen;
    for (const auto& tag : tags) {
        if (!core::is_valid_local_id(tag)) {
            return core::Status::failure(std::string(error_prefix) + ".invalid_hazard_tag",
                                         std::string(label) + " hazard tag is invalid: " + tag);
        }
        if (!seen.insert(tag).second) {
            return core::Status::failure(std::string(error_prefix) + ".duplicate_hazard_tag",
                                         "duplicate " + std::string(label) + " hazard tag: " + tag);
        }
    }
    return core::Status::ok();
}

} // namespace

CargoTransportModes CargoTransportModes::of(std::initializer_list<CargoTransportMode> modes) {
    CargoTransportModes result;
    for (const auto mode : modes) {
        result.add(mode);
    }
    return result;
}

CargoTransportModes CargoTransportModes::from_bits(std::uint32_t bits) noexcept {
    CargoTransportModes result;
    result.bits_ = bits;
    return result;
}

void CargoTransportModes::add(CargoTransportMode mode) noexcept {
    bits_ |= bit(mode);
}

bool CargoTransportModes::allows(CargoTransportMode mode) const noexcept {
    return (bits_ & bit(mode)) != 0;
}

bool CargoTransportModes::empty() const noexcept {
    return bits_ == 0;
}

bool CargoTransportModes::has_unknown_bits() const noexcept {
    return (bits_ & ~known_transport_mode_bits()) != 0;
}

std::uint32_t CargoTransportModes::bits() const noexcept {
    return bits_;
}

core::Status CargoRecord::validate() const {
    if (!cargo_id.is_valid()) {
        return core::Status::failure("cargo.invalid_id", "cargo needs a stable save id");
    }
    if (!prototype_id.is_valid()) {
        return core::Status::failure("cargo.invalid_prototype", "cargo prototype id must be valid");
    }
    if (!position.is_finite()) {
        return core::Status::failure("cargo.invalid_position", "cargo position must be finite");
    }
    if (mass_grams == 0) {
        return core::Status::failure("cargo.invalid_mass", "cargo mass must be non-zero");
    }
    if (volume_milliliters == 0) {
        return core::Status::failure("cargo.invalid_volume", "cargo volume must be non-zero");
    }
    if (stability_per_mille < 0 || stability_per_mille > 1000) {
        return core::Status::failure("cargo.invalid_stability",
                                     "cargo stability must be between 0 and 1000 per mille");
    }
    if (allowed_transport_modes.empty()) {
        return core::Status::failure("cargo.no_transport_modes",
                                     "cargo must declare at least one transport mode");
    }
    if (allowed_transport_modes.has_unknown_bits()) {
        return core::Status::failure(
            "cargo.invalid_transport_mode",
            "cargo contains unsupported transport mode bits: " +
                std::to_string(allowed_transport_modes.bits() & ~known_transport_mode_bits()));
    }
    auto hazard_status = validate_hazard_tags(hazard_tags, "cargo", "cargo");
    if (!hazard_status) {
        return hazard_status;
    }

    return core::Status::ok();
}

bool CargoRecord::is_hazardous() const noexcept {
    return !hazard_tags.empty();
}

core::Status CargoDefinition::validate() const {
    if (!prototype_id.is_valid()) {
        return core::Status::failure("cargo_definition.invalid_prototype",
                                     "cargo definition prototype id must be valid");
    }
    if (mass_grams == 0) {
        return core::Status::failure("cargo_definition.invalid_mass",
                                     "cargo definition mass must be non-zero");
    }
    if (volume_milliliters == 0) {
        return core::Status::failure("cargo_definition.invalid_volume",
                                     "cargo definition volume must be non-zero");
    }
    if (stability_per_mille < 0 || stability_per_mille > 1000) {
        return core::Status::failure("cargo_definition.invalid_stability",
                                     "cargo stability must be between 0 and 1000 per mille");
    }
    if (allowed_transport_modes.empty()) {
        return core::Status::failure("cargo_definition.no_transport_modes",
                                     "cargo definition must declare a transport mode");
    }
    if (allowed_transport_modes.has_unknown_bits()) {
        return core::Status::failure(
            "cargo_definition.invalid_transport_mode",
            "cargo definition contains unsupported transport mode bits: " +
                std::to_string(allowed_transport_modes.bits() & ~known_transport_mode_bits()));
    }
    auto hazard_status = validate_hazard_tags(hazard_tags, "cargo_definition", "cargo definition");
    if (!hazard_status) {
        return hazard_status;
    }
    return core::Status::ok();
}

bool CargoDefinition::is_hazardous() const noexcept {
    return !hazard_tags.empty();
}

core::Result<CargoRecord> CargoDefinition::create_record(core::SaveId cargo_id,
                                                         Vec3 position) const {
    auto status = validate();
    if (!status) {
        return core::Result<CargoRecord>::failure(status.error().code, status.error().message);
    }

    CargoRecord record;
    record.cargo_id = cargo_id;
    record.prototype_id = prototype_id;
    record.position = position;
    record.mass_grams = mass_grams;
    record.volume_milliliters = volume_milliliters;
    record.stability_per_mille = stability_per_mille;
    record.allowed_transport_modes = allowed_transport_modes;
    record.hazard_tags = hazard_tags;

    status = record.validate();
    if (!status) {
        return core::Result<CargoRecord>::failure(status.error().code, status.error().message);
    }
    return core::Result<CargoRecord>::success(std::move(record));
}

core::Result<CargoTransportMode> cargo_transport_mode_from_name(std::string_view name) {
    if (name == "hand") {
        return core::Result<CargoTransportMode>::success(CargoTransportMode::hand);
    }
    if (name == "cart") {
        return core::Result<CargoTransportMode>::success(CargoTransportMode::cart);
    }
    if (name == "wagon") {
        return core::Result<CargoTransportMode>::success(CargoTransportMode::wagon);
    }
    if (name == "boat") {
        return core::Result<CargoTransportMode>::success(CargoTransportMode::boat);
    }
    if (name == "animal") {
        return core::Result<CargoTransportMode>::success(CargoTransportMode::animal);
    }
    if (name == "crane") {
        return core::Result<CargoTransportMode>::success(CargoTransportMode::crane);
    }
    return core::Result<CargoTransportMode>::failure(
        "cargo.invalid_transport_mode", "unsupported cargo transport mode: " + std::string(name));
}

std::string_view cargo_transport_mode_name(CargoTransportMode mode) noexcept {
    switch (mode) {
    case CargoTransportMode::hand:
        return "hand";
    case CargoTransportMode::cart:
        return "cart";
    case CargoTransportMode::wagon:
        return "wagon";
    case CargoTransportMode::boat:
        return "boat";
    case CargoTransportMode::animal:
        return "animal";
    case CargoTransportMode::crane:
        return "crane";
    }
    return "unknown";
}

} // namespace heartstead::cargo
