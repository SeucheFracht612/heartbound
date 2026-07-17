#include "engine/items/item_prototype.hpp"

#include "engine/modding/prototype_registry.hpp"

#include <charconv>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace heartstead::items {

namespace {

[[nodiscard]] const std::string* field(const modding::GenericPrototype& prototype,
                                       std::string_view key) {
    const auto found = prototype.fields.find(std::string(key));
    return found == prototype.fields.end() ? nullptr : &found->second;
}

[[nodiscard]] core::Result<std::uint32_t> parse_positive_u32(std::string_view value,
                                                             std::string_view field_name) {
    std::uint64_t parsed = 0;
    const auto* begin = value.data();
    const auto* end = value.data() + value.size();
    const auto [ptr, error] = std::from_chars(begin, end, parsed);
    if (error != std::errc{} || ptr != end || parsed == 0 ||
        parsed > std::numeric_limits<std::uint32_t>::max()) {
        return core::Result<std::uint32_t>::failure("item_prototype.invalid_number",
                                                    std::string(field_name) +
                                                        " must be a positive 32-bit integer");
    }
    return core::Result<std::uint32_t>::success(static_cast<std::uint32_t>(parsed));
}

[[nodiscard]] core::Result<std::uint64_t> parse_non_negative_u64(std::string_view value,
                                                                 std::string_view field_name) {
    std::uint64_t parsed = 0;
    const auto* begin = value.data();
    const auto* end = value.data() + value.size();
    const auto [ptr, error] = std::from_chars(begin, end, parsed);
    if (error != std::errc{} || ptr != end) {
        return core::Result<std::uint64_t>::failure("item_prototype.invalid_number",
                                                    std::string(field_name) +
                                                        " must be a non-negative integer");
    }
    return core::Result<std::uint64_t>::success(parsed);
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

[[nodiscard]] core::Result<std::vector<std::string>>
parse_tags(const modding::GenericPrototype& prototype) {
    const auto* value = field(prototype, "tags");
    std::vector<std::string> tags;
    if (value == nullptr || value->empty()) {
        return core::Result<std::vector<std::string>>::success(std::move(tags));
    }

    for (const auto tag : split(*value, ',')) {
        if (!core::is_valid_local_id(tag)) {
            return core::Result<std::vector<std::string>>::failure(
                "item_prototype.invalid_tag",
                "tags contains invalid item tag: " + std::string(tag));
        }
        tags.emplace_back(tag);
    }
    return core::Result<std::vector<std::string>>::success(std::move(tags));
}

} // namespace

core::Result<ItemDefinition>
item_definition_from_prototype(const modding::GenericPrototype& prototype) {
    if (prototype.kind != modding::PrototypeKinds::item) {
        return core::Result<ItemDefinition>::failure("item_prototype.kind_mismatch",
                                                     "prototype is not an item");
    }
    if (!prototype.id.is_valid()) {
        return core::Result<ItemDefinition>::failure("item_prototype.invalid_id",
                                                     "item prototype id is invalid");
    }

    const auto* stack_limit_value = field(prototype, "stack_limit");
    if (stack_limit_value == nullptr || stack_limit_value->empty()) {
        return core::Result<ItemDefinition>::failure("item_prototype.missing_stack_limit",
                                                     "item prototype must declare stack_limit");
    }

    auto stack_limit = parse_positive_u32(*stack_limit_value, "stack_limit");
    auto tags = parse_tags(prototype);
    if (!stack_limit) {
        return core::Result<ItemDefinition>::failure(stack_limit.error().code,
                                                     stack_limit.error().message);
    }
    if (!tags) {
        return core::Result<ItemDefinition>::failure(tags.error().code, tags.error().message);
    }

    ItemDefinition definition;
    definition.prototype_id = prototype.id;
    definition.stack_limit = stack_limit.value();
    definition.tags = std::move(tags).value();
    if (const auto* mass = field(prototype, "mass_grams"); mass != nullptr) {
        auto parsed_mass = parse_non_negative_u64(*mass, "mass_grams");
        if (!parsed_mass) {
            return core::Result<ItemDefinition>::failure(parsed_mass.error().code,
                                                         parsed_mass.error().message);
        }
        definition.mass_grams = parsed_mass.value();
    }

    auto status = definition.validate();
    if (!status) {
        return core::Result<ItemDefinition>::failure(status.error().code, status.error().message);
    }
    return core::Result<ItemDefinition>::success(std::move(definition));
}

} // namespace heartstead::items
