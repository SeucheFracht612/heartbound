#include "engine/processes/process_prototype.hpp"

#include "engine/modding/prototype_registry.hpp"

#include <charconv>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace heartstead::processes {

namespace {

[[nodiscard]] const std::string* field(const modding::GenericPrototype& prototype,
                                       std::string_view key) {
    const auto found = prototype.fields.find(std::string(key));
    return found == prototype.fields.end() ? nullptr : &found->second;
}

[[nodiscard]] core::Result<std::int64_t> parse_positive_i64(std::string_view value,
                                                            std::string_view field_name) {
    std::int64_t parsed = 0;
    const auto* begin = value.data();
    const auto* end = value.data() + value.size();
    const auto [ptr, error] = std::from_chars(begin, end, parsed);
    if (error != std::errc{} || ptr != end || parsed <= 0) {
        return core::Result<std::int64_t>::failure("process_prototype.invalid_number",
                                                   std::string(field_name) +
                                                       " must be a positive integer");
    }
    return core::Result<std::int64_t>::success(parsed);
}

[[nodiscard]] core::Result<std::int64_t> parse_i64_range(std::string_view value,
                                                         std::string_view field_name,
                                                         std::int64_t min, std::int64_t max) {
    std::int64_t parsed = 0;
    const auto* begin = value.data();
    const auto* end = value.data() + value.size();
    const auto [ptr, error] = std::from_chars(begin, end, parsed);
    if (error != std::errc{} || ptr != end || parsed < min || parsed > max) {
        return core::Result<std::int64_t>::failure("process_prototype.invalid_number",
                                                   std::string(field_name) +
                                                       " must be an integer in the valid range");
    }
    return core::Result<std::int64_t>::success(parsed);
}

[[nodiscard]] core::Result<std::uint32_t> parse_positive_u32(std::string_view value,
                                                             std::string_view field_name) {
    std::uint64_t parsed = 0;
    const auto* begin = value.data();
    const auto* end = value.data() + value.size();
    const auto [ptr, error] = std::from_chars(begin, end, parsed);
    if (error != std::errc{} || ptr != end || parsed == 0 ||
        parsed > std::numeric_limits<std::uint32_t>::max()) {
        return core::Result<std::uint32_t>::failure("process_prototype.invalid_number",
                                                    std::string(field_name) +
                                                        " must be a positive 32-bit integer");
    }
    return core::Result<std::uint32_t>::success(static_cast<std::uint32_t>(parsed));
}

[[nodiscard]] core::Result<bool> parse_bool(std::string_view value, std::string_view field_name) {
    if (value == "true") {
        return core::Result<bool>::success(true);
    }
    if (value == "false") {
        return core::Result<bool>::success(false);
    }
    return core::Result<bool>::failure("process_prototype.invalid_bool",
                                       std::string(field_name) + " must be true or false");
}

[[nodiscard]] core::Result<bool> parse_optional_bool(const modding::GenericPrototype& prototype,
                                                     std::string_view field_name,
                                                     bool default_value) {
    const auto* value = field(prototype, field_name);
    if (value == nullptr || value->empty()) {
        return core::Result<bool>::success(default_value);
    }
    return parse_bool(*value, field_name);
}

[[nodiscard]] core::Result<std::uint32_t>
parse_optional_positive_u32(const modding::GenericPrototype& prototype, std::string_view field_name,
                            std::uint32_t default_value) {
    const auto* value = field(prototype, field_name);
    if (value == nullptr || value->empty()) {
        return core::Result<std::uint32_t>::success(default_value);
    }
    return parse_positive_u32(*value, field_name);
}

[[nodiscard]] core::Result<std::int64_t>
parse_optional_i64_range(const modding::GenericPrototype& prototype, std::string_view field_name,
                         std::int64_t default_value, std::int64_t min, std::int64_t max) {
    const auto* value = field(prototype, field_name);
    if (value == nullptr || value->empty()) {
        return core::Result<std::int64_t>::success(default_value);
    }
    return parse_i64_range(*value, field_name, min, max);
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
                "process_prototype.invalid_tag",
                "tags contains invalid process tag: " + std::string(tag));
        }
        tags.emplace_back(tag);
    }
    return core::Result<std::vector<std::string>>::success(std::move(tags));
}

} // namespace

core::Result<ProcessDefinition>
process_definition_from_prototype(const modding::GenericPrototype& prototype) {
    if (prototype.kind != modding::PrototypeKinds::process) {
        return core::Result<ProcessDefinition>::failure("process_prototype.kind_mismatch",
                                                        "prototype is not a process");
    }
    if (!prototype.id.is_valid()) {
        return core::Result<ProcessDefinition>::failure("process_prototype.invalid_id",
                                                        "process prototype id is invalid");
    }

    const auto* work_value = field(prototype, "default_required_work_ms");
    if (work_value == nullptr || work_value->empty()) {
        return core::Result<ProcessDefinition>::failure(
            "process_prototype.missing_required_work",
            "process prototype must declare default_required_work_ms");
    }

    auto required_work = parse_positive_i64(*work_value, "default_required_work_ms");
    auto requires_room = parse_optional_bool(prototype, "requires_room", false);
    auto requires_power = parse_optional_bool(prototype, "requires_power", false);
    auto required_power_capacity =
        parse_optional_positive_u32(prototype, "required_power_capacity", 1);
    auto base_quality_rate =
        parse_optional_i64_range(prototype, "base_quality_rate_per_mille", 1000, 0, 10000);
    auto tags = parse_tags(prototype);
    if (!required_work) {
        return core::Result<ProcessDefinition>::failure(required_work.error().code,
                                                        required_work.error().message);
    }
    if (!requires_room) {
        return core::Result<ProcessDefinition>::failure(requires_room.error().code,
                                                        requires_room.error().message);
    }
    if (!requires_power) {
        return core::Result<ProcessDefinition>::failure(requires_power.error().code,
                                                        requires_power.error().message);
    }
    if (!required_power_capacity) {
        return core::Result<ProcessDefinition>::failure(required_power_capacity.error().code,
                                                        required_power_capacity.error().message);
    }
    if (!base_quality_rate) {
        return core::Result<ProcessDefinition>::failure(base_quality_rate.error().code,
                                                        base_quality_rate.error().message);
    }
    if (!tags) {
        return core::Result<ProcessDefinition>::failure(tags.error().code, tags.error().message);
    }

    ProcessDefinition definition;
    definition.prototype_id = prototype.id;
    definition.default_required_work_ms = required_work.value();
    definition.requires_room = requires_room.value();
    definition.requires_power = requires_power.value();
    definition.required_power_capacity = required_power_capacity.value();
    definition.base_quality_rate_per_mille = base_quality_rate.value();
    definition.tags = std::move(tags).value();

    auto status = definition.validate();
    if (!status) {
        return core::Result<ProcessDefinition>::failure(status.error().code,
                                                        status.error().message);
    }
    return core::Result<ProcessDefinition>::success(std::move(definition));
}

} // namespace heartstead::processes
