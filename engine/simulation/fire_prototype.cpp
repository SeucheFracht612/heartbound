#include "engine/simulation/fire_prototype.hpp"

#include "engine/modding/prototype_registry.hpp"

#include <charconv>
#include <cmath>
#include <limits>
#include <string>
#include <system_error>

namespace heartstead::simulation {

namespace {

[[nodiscard]] const std::string* field(const modding::GenericPrototype& prototype,
                                       std::string_view key) {
    const auto found = prototype.fields.find(std::string(key));
    return found == prototype.fields.end() ? nullptr : &found->second;
}

[[nodiscard]] core::Result<std::uint64_t> u64_field(const modding::GenericPrototype& prototype,
                                                    std::string_view key,
                                                    std::uint64_t fallback = 0) {
    const auto* value = field(prototype, key);
    if (value == nullptr)
        return core::Result<std::uint64_t>::success(fallback);
    std::uint64_t parsed = 0;
    const auto [end, error] = std::from_chars(value->data(), value->data() + value->size(), parsed);
    if (error != std::errc{} || end != value->data() + value->size()) {
        return core::Result<std::uint64_t>::failure("fire_prototype.invalid_number",
                                                    std::string(key) + " must be u64");
    }
    return core::Result<std::uint64_t>::success(parsed);
}

[[nodiscard]] core::Result<float> float_field(const modding::GenericPrototype& prototype,
                                              std::string_view key, float fallback) {
    const auto* value = field(prototype, key);
    if (value == nullptr)
        return core::Result<float>::success(fallback);
    float parsed = 0.0F;
    const auto [end, error] = std::from_chars(value->data(), value->data() + value->size(), parsed);
    if (error != std::errc{} || end != value->data() + value->size() || !std::isfinite(parsed)) {
        return core::Result<float>::failure("fire_prototype.invalid_number",
                                            std::string(key) + " must be finite");
    }
    return core::Result<float>::success(parsed);
}

} // namespace

core::Result<FireDefinition>
fire_definition_from_prototype(const modding::GenericPrototype& prototype) {
    if (prototype.kind != modding::PrototypeKinds::fire) {
        return core::Result<FireDefinition>::failure("fire_prototype.invalid_kind",
                                                     "prototype kind must be fire");
    }
    auto ember = u64_field(prototype, "ember_window_ticks");
    auto fuel = u64_field(prototype, "maximum_fuel_buffer_ticks");
    auto light = u64_field(prototype, "light_level");
    auto warmth = float_field(prototype, "warmth_radius", 0.0F);
    auto repel = float_field(prototype, "repel_radius", 0.0F);
    auto slots = u64_field(prototype, "cook_slot_count");
    if (!ember || !fuel || !light || !warmth || !repel || !slots || light.value() > 255 ||
        slots.value() > 255) {
        return core::Result<FireDefinition>::failure("fire_prototype.invalid_fields",
                                                     "fire prototype fields are invalid");
    }
    FireDefinition definition{prototype.id,
                              ember.value(),
                              fuel.value(),
                              static_cast<std::uint8_t>(light.value()),
                              warmth.value(),
                              repel.value(),
                              static_cast<std::uint8_t>(slots.value())};
    auto status = definition.validate();
    if (!status)
        return core::Result<FireDefinition>::failure(status.error().code, status.error().message);
    return core::Result<FireDefinition>::success(definition);
}

} // namespace heartstead::simulation
