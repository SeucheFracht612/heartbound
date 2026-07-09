#include "engine/rooms/room_descriptor_prototype.hpp"

#include "engine/modding/prototype_registry.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace heartstead::rooms {

namespace {

[[nodiscard]] const std::string* field(const modding::GenericPrototype& prototype,
                                       std::string_view key) {
    const auto found = prototype.fields.find(std::string(key));
    return found == prototype.fields.end() ? nullptr : &found->second;
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
                "room_descriptor_prototype.invalid_tag",
                "tags contains invalid room descriptor tag: " + std::string(tag));
        }
        tags.emplace_back(tag);
    }
    return core::Result<std::vector<std::string>>::success(std::move(tags));
}

} // namespace

core::Status RoomDescriptorDefinition::validate() const {
    if (!prototype_id.is_valid()) {
        return core::Status::failure("room_descriptor_definition.invalid_prototype",
                                     "room descriptor definition prototype id must be valid");
    }
    if (!core::is_valid_local_id(code)) {
        return core::Status::failure("room_descriptor_definition.invalid_code",
                                     "room descriptor code must be a valid local id");
    }
    if (label.empty()) {
        return core::Status::failure("room_descriptor_definition.missing_label",
                                     "room descriptor label must be non-empty");
    }
    for (const auto& tag : tags) {
        if (!core::is_valid_local_id(tag)) {
            return core::Status::failure("room_descriptor_definition.invalid_tag",
                                         "room descriptor tag is invalid: " + tag);
        }
    }
    return core::Status::ok();
}

RoomDescriptor RoomDescriptorDefinition::descriptor() const {
    return RoomDescriptor{code, label, severity};
}

core::Result<RoomDescriptorDefinition>
room_descriptor_definition_from_prototype(const modding::GenericPrototype& prototype) {
    if (prototype.kind != modding::PrototypeKinds::room_descriptor) {
        return core::Result<RoomDescriptorDefinition>::failure(
            "room_descriptor_prototype.kind_mismatch", "prototype is not a room descriptor");
    }
    if (!prototype.id.is_valid()) {
        return core::Result<RoomDescriptorDefinition>::failure(
            "room_descriptor_prototype.invalid_id", "room descriptor prototype id is invalid");
    }

    const auto* code_value = field(prototype, "code");
    if (code_value == nullptr || code_value->empty()) {
        return core::Result<RoomDescriptorDefinition>::failure(
            "room_descriptor_prototype.missing_code",
            "room descriptor prototype must declare code");
    }
    const auto* label_value = field(prototype, "label");

    const auto* severity_value = field(prototype, "severity");
    if (severity_value == nullptr || severity_value->empty()) {
        return core::Result<RoomDescriptorDefinition>::failure(
            "room_descriptor_prototype.missing_severity",
            "room descriptor prototype must declare severity");
    }

    auto severity = room_descriptor_severity_from_name(*severity_value);
    auto tags = parse_tags(prototype);
    if (!severity) {
        return core::Result<RoomDescriptorDefinition>::failure(severity.error().code,
                                                               severity.error().message);
    }
    if (!tags) {
        return core::Result<RoomDescriptorDefinition>::failure(tags.error().code,
                                                               tags.error().message);
    }

    RoomDescriptorDefinition definition;
    definition.prototype_id = prototype.id;
    definition.code = *code_value;
    definition.label =
        label_value != nullptr && !label_value->empty() ? *label_value : prototype.display_name;
    definition.severity = severity.value();
    definition.tags = std::move(tags).value();

    auto status = definition.validate();
    if (!status) {
        return core::Result<RoomDescriptorDefinition>::failure(status.error().code,
                                                               status.error().message);
    }
    return core::Result<RoomDescriptorDefinition>::success(std::move(definition));
}

} // namespace heartstead::rooms
