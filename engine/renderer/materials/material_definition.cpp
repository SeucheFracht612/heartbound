#include "engine/renderer/materials/material_definition.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <utility>

namespace heartstead::renderer::materials {

namespace {

[[nodiscard]] bool is_valid_material_name(std::string_view name) noexcept {
    if (name.empty() || name.front() == '.' || name.back() == '.') {
        return false;
    }
    for (const auto character : name) {
        const auto valid = (character >= 'a' && character <= 'z') ||
                           (character >= '0' && character <= '9') || character == '_' ||
                           character == '-' || character == '.';
        if (!valid) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool is_valid_virtual_path(const assets::VirtualPath& path) noexcept {
    return core::is_valid_namespace_id(path.namespace_id) &&
           core::is_valid_local_id(path.relative_path.generic_string());
}

[[nodiscard]] bool is_valid_color(MaterialColor color) noexcept {
    return std::isfinite(color.red) && std::isfinite(color.green) && std::isfinite(color.blue) &&
           std::isfinite(color.alpha) && color.red >= 0.0F && color.green >= 0.0F &&
           color.blue >= 0.0F && color.alpha >= 0.0F && color.red <= 1.0F && color.green <= 1.0F &&
           color.blue <= 1.0F && color.alpha <= 1.0F;
}

[[nodiscard]] core::Status insert_parameter_name(std::unordered_set<std::string>& names,
                                                 std::string_view name) {
    if (!is_valid_material_name(name)) {
        return core::Status::failure("material.invalid_parameter_name",
                                     "material parameter name is invalid");
    }
    if (!names.insert(std::string(name)).second) {
        return core::Status::failure("material.duplicate_parameter",
                                     "material parameter name is duplicated: " + std::string(name));
    }
    return core::Status::ok();
}

} // namespace

core::Status MaterialRegistry::add(MaterialDefinition definition) {
    auto status = validate_material_definition(definition);
    if (!status) {
        return status;
    }
    if (find(definition.id.value()) != nullptr) {
        return core::Status::failure("material.duplicate_definition",
                                     "material definition id is duplicated: " +
                                         definition.id.value());
    }
    definitions_.push_back(std::move(definition));
    return core::Status::ok();
}

const MaterialDefinition* MaterialRegistry::find(std::string_view material_id) const noexcept {
    const auto found =
        std::ranges::find_if(definitions_, [material_id](const MaterialDefinition& definition) {
            return definition.id.value() == material_id;
        });
    return found == definitions_.end() ? nullptr : &*found;
}

std::vector<const MaterialDefinition*> MaterialRegistry::definitions() const {
    std::vector<const MaterialDefinition*> result;
    result.reserve(definitions_.size());
    for (const auto& definition : definitions_) {
        result.push_back(&definition);
    }
    return result;
}

std::size_t MaterialRegistry::count_domain(MaterialDomain domain) const noexcept {
    return static_cast<std::size_t>(
        std::ranges::count_if(definitions_, [domain](const MaterialDefinition& definition) {
            return definition.domain == domain;
        }));
}

std::size_t MaterialRegistry::size() const noexcept {
    return definitions_.size();
}

bool MaterialRegistry::empty() const noexcept {
    return definitions_.empty();
}

core::Status validate_material_definition(const MaterialDefinition& definition) {
    if (!definition.id.is_valid()) {
        return core::Status::failure("material.invalid_id",
                                     "material definition id must be namespace:local_id");
    }
    if (!is_valid_virtual_path(definition.shader_template)) {
        return core::Status::failure("material.invalid_shader_template",
                                     "material shader template must be a valid virtual path");
    }

    std::unordered_set<std::string> parameter_names;
    for (const auto& texture : definition.textures) {
        auto status = insert_parameter_name(parameter_names, texture.name);
        if (!status) {
            return status;
        }
        if (!is_valid_virtual_path(texture.texture)) {
            return core::Status::failure("material.invalid_texture",
                                         "material texture binding must be a valid virtual path");
        }
    }

    for (const auto& scalar : definition.scalars) {
        auto status = insert_parameter_name(parameter_names, scalar.name);
        if (!status) {
            return status;
        }
        if (!std::isfinite(scalar.value)) {
            return core::Status::failure("material.invalid_scalar",
                                         "material scalar parameter must be finite");
        }
    }

    for (const auto& color : definition.colors) {
        auto status = insert_parameter_name(parameter_names, color.name);
        if (!status) {
            return status;
        }
        if (!is_valid_color(color.value)) {
            return core::Status::failure(
                "material.invalid_color",
                "material color parameter values must be finite and between 0 and 1");
        }
    }

    return core::Status::ok();
}

std::optional<MaterialDomain> parse_material_domain(std::string_view value) noexcept {
    if (value == "surface") {
        return MaterialDomain::surface;
    }
    if (value == "terrain") {
        return MaterialDomain::terrain;
    }
    if (value == "foliage") {
        return MaterialDomain::foliage;
    }
    if (value == "water") {
        return MaterialDomain::water;
    }
    if (value == "particle") {
        return MaterialDomain::particle;
    }
    if (value == "ui") {
        return MaterialDomain::ui;
    }
    return std::nullopt;
}

std::optional<MaterialBlendMode> parse_material_blend_mode(std::string_view value) noexcept {
    if (value == "opaque") {
        return MaterialBlendMode::opaque;
    }
    if (value == "masked") {
        return MaterialBlendMode::masked;
    }
    if (value == "translucent") {
        return MaterialBlendMode::translucent;
    }
    if (value == "additive") {
        return MaterialBlendMode::additive;
    }
    return std::nullopt;
}

std::string_view material_domain_name(MaterialDomain domain) noexcept {
    switch (domain) {
    case MaterialDomain::surface:
        return "surface";
    case MaterialDomain::terrain:
        return "terrain";
    case MaterialDomain::foliage:
        return "foliage";
    case MaterialDomain::water:
        return "water";
    case MaterialDomain::particle:
        return "particle";
    case MaterialDomain::ui:
        return "ui";
    }
    return "unknown";
}

std::string_view material_blend_mode_name(MaterialBlendMode blend_mode) noexcept {
    switch (blend_mode) {
    case MaterialBlendMode::opaque:
        return "opaque";
    case MaterialBlendMode::masked:
        return "masked";
    case MaterialBlendMode::translucent:
        return "translucent";
    case MaterialBlendMode::additive:
        return "additive";
    }
    return "unknown";
}

} // namespace heartstead::renderer::materials
