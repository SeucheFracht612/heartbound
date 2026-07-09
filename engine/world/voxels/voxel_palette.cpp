#include "engine/world/voxels/voxel_palette.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace heartstead::world {

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

[[nodiscard]] core::Status validate_token(std::string_view token, std::string_view field_name) {
    if (core::is_valid_local_id(token)) {
        return core::Status::ok();
    }
    return core::Status::failure("voxel_palette.invalid_token",
                                 std::string(field_name) + " must be a valid token");
}

[[nodiscard]] core::Status validate_token_list(const std::vector<std::string>& tokens,
                                               std::string_view field_name) {
    for (const auto& token : tokens) {
        auto status = validate_token(token, field_name);
        if (!status) {
            return status;
        }
    }
    return core::Status::ok();
}

[[nodiscard]] core::Result<std::string> required_field(const modding::GenericPrototype& prototype,
                                                       std::string_view key) {
    const auto* value = field(prototype, key);
    if (value == nullptr || value->empty()) {
        return core::Result<std::string>::failure("voxel_palette.missing_field",
                                                  prototype.id.value() + ": missing voxel field " +
                                                      std::string(key));
    }
    return core::Result<std::string>::success(*value);
}

[[nodiscard]] std::vector<std::string> parse_tags(const modding::GenericPrototype& prototype) {
    std::vector<std::string> result;
    const auto* tags = field(prototype, "tags");
    if (tags == nullptr || tags->empty()) {
        return result;
    }
    for (const auto tag : split(*tags, ',')) {
        result.emplace_back(tag);
    }
    return result;
}

} // namespace

core::Status VoxelDefinition::validate() const {
    if (type == air_type) {
        return core::Status::failure("voxel_palette.reserved_type",
                                     "voxel type 0 is reserved for air");
    }
    if (!prototype_id.is_valid()) {
        return core::Status::failure("voxel_palette.invalid_prototype",
                                     "voxel prototype id must be valid");
    }
    if (display_name.empty()) {
        return core::Status::failure("voxel_palette.missing_display_name",
                                     "voxel display name must not be empty");
    }
    auto status = validate_token(terrain_material, "voxel terrain material");
    if (!status) {
        return status;
    }
    status = validate_token(mining_tool, "voxel mining tool");
    if (!status) {
        return status;
    }
    return validate_token_list(tags, "voxel tag");
}

core::Status VoxelPalette::add(VoxelDefinition definition) {
    auto status = definition.validate();
    if (!status) {
        return status;
    }
    if (by_type_.contains(definition.type)) {
        return core::Status::failure("voxel_palette.duplicate_type",
                                     "duplicate voxel type: " + std::to_string(definition.type));
    }
    if (by_prototype_.contains(definition.prototype_id.value())) {
        return core::Status::failure("voxel_palette.duplicate_prototype",
                                     "duplicate voxel prototype: " +
                                         definition.prototype_id.value());
    }

    const auto index = definitions_.size();
    by_type_.emplace(definition.type, index);
    by_prototype_.emplace(definition.prototype_id.value(), index);
    definitions_.push_back(std::move(definition));
    return core::Status::ok();
}

const VoxelDefinition* VoxelPalette::find_by_type(std::uint16_t type) const noexcept {
    const auto found = by_type_.find(type);
    return found == by_type_.end() ? nullptr : &definitions_[found->second];
}

const VoxelDefinition*
VoxelPalette::find_by_prototype(const core::PrototypeId& prototype_id) const noexcept {
    const auto found = by_prototype_.find(prototype_id.value());
    return found == by_prototype_.end() ? nullptr : &definitions_[found->second];
}

std::optional<std::uint16_t>
VoxelPalette::type_for(const core::PrototypeId& prototype_id) const noexcept {
    const auto* definition = find_by_prototype(prototype_id);
    if (definition == nullptr) {
        return std::nullopt;
    }
    return definition->type;
}

core::Result<VoxelCell> VoxelPalette::cell_for(const core::PrototypeId& prototype_id,
                                               std::uint8_t light) const {
    const auto type = type_for(prototype_id);
    if (!type) {
        return core::Result<VoxelCell>::failure("voxel_palette.missing_prototype",
                                                "voxel prototype is not in the palette: " +
                                                    prototype_id.value());
    }
    return core::Result<VoxelCell>::success(VoxelCell{type.value(), light});
}

std::vector<const VoxelDefinition*> VoxelPalette::definitions() const {
    std::vector<const VoxelDefinition*> result;
    result.reserve(definitions_.size());
    for (const auto& definition : definitions_) {
        result.push_back(&definition);
    }
    return result;
}

std::size_t VoxelPalette::size() const noexcept {
    return definitions_.size();
}

bool VoxelPalette::empty() const noexcept {
    return definitions_.empty();
}

core::Result<VoxelDefinition>
voxel_definition_from_prototype(const modding::GenericPrototype& prototype, std::uint16_t type) {
    if (prototype.kind != modding::PrototypeKinds::voxel) {
        return core::Result<VoxelDefinition>::failure(
            "voxel_palette.invalid_kind", prototype.id.value() + ": prototype kind must be voxel");
    }

    auto terrain_material = required_field(prototype, "terrain_material");
    if (!terrain_material) {
        return core::Result<VoxelDefinition>::failure(terrain_material.error().code,
                                                      terrain_material.error().message);
    }
    auto mining_tool = required_field(prototype, "mining_tool");
    if (!mining_tool) {
        return core::Result<VoxelDefinition>::failure(mining_tool.error().code,
                                                      mining_tool.error().message);
    }

    VoxelDefinition definition;
    definition.type = type;
    definition.prototype_id = prototype.id;
    definition.display_name = prototype.display_name;
    definition.terrain_material = std::move(terrain_material).value();
    definition.mining_tool = std::move(mining_tool).value();
    definition.tags = parse_tags(prototype);

    auto status = definition.validate();
    if (!status) {
        return core::Result<VoxelDefinition>::failure(
            status.error().code, prototype.id.value() + ": " + status.error().message);
    }
    return core::Result<VoxelDefinition>::success(std::move(definition));
}

core::Result<VoxelPalette>
voxel_palette_from_prototypes(const modding::PrototypeRegistry& prototypes) {
    auto voxel_prototypes = prototypes.prototypes_of_kind(modding::PrototypeKinds::voxel);
    std::ranges::sort(voxel_prototypes, {}, [](const modding::GenericPrototype* prototype) {
        return prototype->id.value();
    });

    if (voxel_prototypes.size() >
        static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max())) {
        return core::Result<VoxelPalette>::failure(
            "voxel_palette.too_many_voxels",
            "voxel palette cannot contain more than 65535 content voxel definitions");
    }

    VoxelPalette palette;
    auto next_type = VoxelPalette::first_content_type;
    for (const auto* prototype : voxel_prototypes) {
        auto definition = voxel_definition_from_prototype(*prototype, next_type);
        if (!definition) {
            return core::Result<VoxelPalette>::failure(definition.error().code,
                                                       definition.error().message);
        }
        auto status = palette.add(std::move(definition).value());
        if (!status) {
            return core::Result<VoxelPalette>::failure(status.error().code, status.error().message);
        }
        ++next_type;
    }

    return core::Result<VoxelPalette>::success(std::move(palette));
}

} // namespace heartstead::world
