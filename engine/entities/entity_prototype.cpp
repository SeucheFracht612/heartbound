#include "engine/entities/entity_prototype.hpp"

#include "engine/modding/prototype_registry.hpp"

#include <charconv>
#include <string>
#include <string_view>
#include <utility>

namespace heartstead::entities {

namespace {

[[nodiscard]] const std::string* field(const modding::GenericPrototype& prototype,
                                       std::string_view key) {
    const auto found = prototype.fields.find(std::string(key));
    return found == prototype.fields.end() ? nullptr : &found->second;
}

[[nodiscard]] core::Result<bool> parse_bool(std::string_view value, std::string_view field_name) {
    if (value == "true") {
        return core::Result<bool>::success(true);
    }
    if (value == "false") {
        return core::Result<bool>::success(false);
    }
    return core::Result<bool>::failure("entity_prototype.invalid_bool",
                                       std::string(field_name) + " must be true or false");
}

} // namespace

core::Result<EntityDefinition>
entity_definition_from_prototype(const modding::GenericPrototype& prototype) {
    if (prototype.kind != modding::PrototypeKinds::entity) {
        return core::Result<EntityDefinition>::failure("entity_prototype.kind_mismatch",
                                                       "prototype is not an entity");
    }
    if (!prototype.id.is_valid()) {
        return core::Result<EntityDefinition>::failure("entity_prototype.invalid_id",
                                                       "entity prototype id is invalid");
    }

    const auto* kind_value = field(prototype, "entity_kind");
    if (kind_value == nullptr || kind_value->empty()) {
        return core::Result<EntityDefinition>::failure("entity_prototype.missing_kind",
                                                       "entity prototype must declare entity_kind");
    }

    auto kind = entity_kind_from_name(*kind_value);
    if (!kind) {
        return core::Result<EntityDefinition>::failure(kind.error().code, kind.error().message);
    }

    bool persistent = false;
    if (const auto* persistent_value = field(prototype, "persistent");
        persistent_value != nullptr && !persistent_value->empty()) {
        auto parsed = parse_bool(*persistent_value, "persistent");
        if (!parsed) {
            return core::Result<EntityDefinition>::failure(parsed.error().code,
                                                           parsed.error().message);
        }
        persistent = parsed.value();
    }

    std::uint64_t carry_capacity_grams = 0;
    if (const auto* capacity_value = field(prototype, "carry_capacity_grams");
        capacity_value != nullptr && !capacity_value->empty()) {
        const auto [end, error] =
            std::from_chars(capacity_value->data(),
                            capacity_value->data() + capacity_value->size(),
                            carry_capacity_grams);
        if (error != std::errc{} || end != capacity_value->data() + capacity_value->size() ||
            carry_capacity_grams == 0) {
            return core::Result<EntityDefinition>::failure(
                "entity_prototype.invalid_capacity",
                "carry_capacity_grams must be a positive integer");
        }
    }

    EntityDefinition definition;
    definition.prototype_id = prototype.id;
    definition.kind = kind.value();
    definition.persistent = persistent;
    definition.carry_capacity_grams = carry_capacity_grams;

    auto status = definition.validate();
    if (!status) {
        return core::Result<EntityDefinition>::failure(status.error().code, status.error().message);
    }
    return core::Result<EntityDefinition>::success(std::move(definition));
}

} // namespace heartstead::entities
