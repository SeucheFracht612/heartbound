#include "engine/cargo/cargo_prototype.hpp"

#include "engine/modding/prototype_registry.hpp"

#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace heartstead::cargo {

namespace {

[[nodiscard]] const std::string* field(const modding::GenericPrototype& prototype,
                                       std::string_view key) {
    const auto found = prototype.fields.find(std::string(key));
    return found == prototype.fields.end() ? nullptr : &found->second;
}

[[nodiscard]] core::Result<std::uint64_t> parse_positive_u64(std::string_view value,
                                                             std::string_view field_name) {
    std::uint64_t parsed = 0;
    const auto* begin = value.data();
    const auto* end = value.data() + value.size();
    const auto [ptr, error] = std::from_chars(begin, end, parsed);
    if (error != std::errc{} || ptr != end || parsed == 0) {
        return core::Result<std::uint64_t>::failure("cargo_prototype.invalid_number",
                                                    std::string(field_name) +
                                                        " must be a positive integer");
    }
    return core::Result<std::uint64_t>::success(parsed);
}

[[nodiscard]] core::Result<std::int32_t> parse_stability(std::string_view value) {
    std::int64_t parsed = 0;
    const auto* begin = value.data();
    const auto* end = value.data() + value.size();
    const auto [ptr, error] = std::from_chars(begin, end, parsed);
    if (error != std::errc{} || ptr != end || parsed < 0 || parsed > 1000) {
        return core::Result<std::int32_t>::failure(
            "cargo_prototype.invalid_stability", "stability_per_mille must be between 0 and 1000");
    }
    return core::Result<std::int32_t>::success(static_cast<std::int32_t>(parsed));
}

[[nodiscard]] std::vector<std::string_view> split(std::string_view value, char delimiter) {
    std::vector<std::string_view> result;
    std::size_t start = 0;
    while (start <= value.size()) {
        const auto end = value.find(delimiter, start);
        if (end == std::string_view::npos) {
            result.push_back(value.substr(start));
            break;
        }
        result.push_back(value.substr(start, end - start));
        start = end + 1;
    }
    return result;
}

[[nodiscard]] core::Result<CargoTransportModes>
parse_transport_modes(const modding::GenericPrototype& prototype) {
    const auto* value = field(prototype, "transport_modes");
    if (value == nullptr || value->empty()) {
        return core::Result<CargoTransportModes>::failure(
            "cargo_prototype.missing_transport_modes",
            "cargo prototype must declare transport_modes");
    }

    CargoTransportModes modes;
    for (const auto mode_name : split(*value, ',')) {
        auto mode = cargo_transport_mode_from_name(mode_name);
        if (!mode) {
            return core::Result<CargoTransportModes>::failure(mode.error().code,
                                                              mode.error().message);
        }
        modes.add(mode.value());
    }
    return core::Result<CargoTransportModes>::success(modes);
}

[[nodiscard]] core::Result<std::vector<std::string>>
parse_hazard_tags(const modding::GenericPrototype& prototype) {
    const auto* value = field(prototype, "hazard_tags");
    std::vector<std::string> tags;
    if (value == nullptr || value->empty()) {
        return core::Result<std::vector<std::string>>::success(std::move(tags));
    }

    for (const auto tag : split(*value, ',')) {
        if (!core::is_valid_local_id(tag)) {
            return core::Result<std::vector<std::string>>::failure(
                "cargo_prototype.invalid_hazard_tag",
                "hazard_tags contains invalid cargo hazard tag: " + std::string(tag));
        }
        tags.emplace_back(tag);
    }
    return core::Result<std::vector<std::string>>::success(std::move(tags));
}

} // namespace

core::Result<CargoDefinition>
cargo_definition_from_prototype(const modding::GenericPrototype& prototype) {
    if (prototype.kind != modding::PrototypeKinds::cargo) {
        return core::Result<CargoDefinition>::failure("cargo_prototype.kind_mismatch",
                                                      "prototype is not cargo");
    }
    if (!prototype.id.is_valid()) {
        return core::Result<CargoDefinition>::failure("cargo_prototype.invalid_id",
                                                      "cargo prototype id is invalid");
    }

    const auto* mass_value = field(prototype, "mass_grams");
    const auto* volume_value = field(prototype, "volume_milliliters");
    if (mass_value == nullptr || mass_value->empty()) {
        return core::Result<CargoDefinition>::failure("cargo_prototype.missing_mass",
                                                      "cargo prototype must declare mass_grams");
    }
    if (volume_value == nullptr || volume_value->empty()) {
        return core::Result<CargoDefinition>::failure(
            "cargo_prototype.missing_volume", "cargo prototype must declare volume_milliliters");
    }

    auto mass = parse_positive_u64(*mass_value, "mass_grams");
    auto volume = parse_positive_u64(*volume_value, "volume_milliliters");
    auto modes = parse_transport_modes(prototype);
    auto hazards = parse_hazard_tags(prototype);
    if (!mass) {
        return core::Result<CargoDefinition>::failure(mass.error().code, mass.error().message);
    }
    if (!volume) {
        return core::Result<CargoDefinition>::failure(volume.error().code, volume.error().message);
    }
    if (!modes) {
        return core::Result<CargoDefinition>::failure(modes.error().code, modes.error().message);
    }
    if (!hazards) {
        return core::Result<CargoDefinition>::failure(hazards.error().code,
                                                      hazards.error().message);
    }

    CargoDefinition definition;
    definition.prototype_id = prototype.id;
    definition.mass_grams = mass.value();
    definition.volume_milliliters = volume.value();
    definition.allowed_transport_modes = modes.value();
    definition.hazard_tags = std::move(hazards).value();

    if (const auto* stability_value = field(prototype, "stability_per_mille");
        stability_value != nullptr && !stability_value->empty()) {
        auto stability = parse_stability(*stability_value);
        if (!stability) {
            return core::Result<CargoDefinition>::failure(stability.error().code,
                                                          stability.error().message);
        }
        definition.stability_per_mille = stability.value();
    }

    auto status = definition.validate();
    if (!status) {
        return core::Result<CargoDefinition>::failure(status.error().code, status.error().message);
    }
    return core::Result<CargoDefinition>::success(std::move(definition));
}

} // namespace heartstead::cargo
