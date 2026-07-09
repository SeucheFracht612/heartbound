#include "engine/renderer/materials/material_prototype_loader.hpp"

#include "engine/assets/virtual_file_system.hpp"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace heartstead::renderer::materials {

namespace {

constexpr std::string_view texture_prefix = "texture.";
constexpr std::string_view optional_texture_prefix = "optional_texture.";
constexpr std::string_view scalar_prefix = "scalar.";
constexpr std::string_view color_prefix = "color.";

[[nodiscard]] bool is_allowed_metadata_field(std::string_view key) noexcept {
    return key == "kind" || key == "id" || key == "display_name" || key == "domain" ||
           key == "blend_mode" || key == "shader_template" || key == "double_sided" ||
           key == "tags";
}

[[nodiscard]] bool starts_with(std::string_view value, std::string_view prefix) noexcept {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

[[nodiscard]] const std::string* field(const modding::GenericPrototype& prototype,
                                       std::string_view key) {
    const auto found = prototype.fields.find(std::string(key));
    return found == prototype.fields.end() ? nullptr : &found->second;
}

[[nodiscard]] core::Result<std::string_view>
required_field(const modding::GenericPrototype& prototype, std::string_view key) {
    const auto* value = field(prototype, key);
    if (value == nullptr || value->empty()) {
        return core::Result<std::string_view>::failure(
            "material_prototype.missing_field",
            prototype.id.value() + ": material prototype is missing field " + std::string(key));
    }
    return core::Result<std::string_view>::success(*value);
}

[[nodiscard]] std::string_view trim(std::string_view value) noexcept {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

[[nodiscard]] std::vector<std::string_view> split(std::string_view value, char delimiter) {
    std::vector<std::string_view> result;
    std::size_t start = 0;
    while (start <= value.size()) {
        const auto end = value.find(delimiter, start);
        if (end == std::string_view::npos) {
            result.push_back(trim(value.substr(start)));
            break;
        }
        result.push_back(trim(value.substr(start, end - start)));
        start = end + 1;
    }
    return result;
}

[[nodiscard]] core::Result<bool> parse_bool(std::string_view value,
                                            const modding::GenericPrototype& prototype,
                                            std::string_view key) {
    if (value == "true") {
        return core::Result<bool>::success(true);
    }
    if (value == "false") {
        return core::Result<bool>::success(false);
    }
    return core::Result<bool>::failure("material_prototype.invalid_bool",
                                       prototype.id.value() + ": " + std::string(key) +
                                           " must be true or false");
}

[[nodiscard]] bool parse_float(std::string_view value, float& output) {
    const auto text = std::string(trim(value));
    if (text.empty()) {
        return false;
    }

    char* end = nullptr;
    errno = 0;
    const auto parsed = std::strtof(text.c_str(), &end);
    if (errno == ERANGE || end == nullptr || *end != '\0' || !std::isfinite(parsed)) {
        return false;
    }

    output = parsed;
    return true;
}

[[nodiscard]] core::Result<MaterialColor> parse_color(std::string_view value,
                                                      const modding::GenericPrototype& prototype,
                                                      std::string_view key) {
    const auto parts = split(value, ',');
    if (parts.size() != 4) {
        return core::Result<MaterialColor>::failure("material_prototype.invalid_color",
                                                    prototype.id.value() + ": " + std::string(key) +
                                                        " must be four comma-separated floats");
    }

    MaterialColor color;
    if (!parse_float(parts[0], color.red) || !parse_float(parts[1], color.green) ||
        !parse_float(parts[2], color.blue) || !parse_float(parts[3], color.alpha)) {
        return core::Result<MaterialColor>::failure("material_prototype.invalid_color",
                                                    prototype.id.value() + ": " + std::string(key) +
                                                        " contains a non-finite float");
    }
    return core::Result<MaterialColor>::success(color);
}

[[nodiscard]] core::Result<assets::VirtualPath>
parse_virtual_path(std::string_view value, const modding::GenericPrototype& prototype,
                   std::string_view key, std::string_view error_code) {
    auto parsed = assets::VirtualPath::parse(value);
    if (!parsed) {
        return core::Result<assets::VirtualPath>::failure(
            std::string(error_code),
            prototype.id.value() + ": " + std::string(key) + " is not a valid virtual path");
    }
    return parsed;
}

[[nodiscard]] std::vector<std::string>
sorted_field_keys(const modding::GenericPrototype& prototype) {
    std::vector<std::string> keys;
    keys.reserve(prototype.fields.size());
    for (const auto& entry : prototype.fields) {
        keys.push_back(entry.first);
    }
    std::ranges::sort(keys);
    return keys;
}

} // namespace

core::Result<MaterialDefinition>
material_definition_from_prototype(const modding::GenericPrototype& prototype) {
    if (prototype.kind != modding::PrototypeKinds::material) {
        return core::Result<MaterialDefinition>::failure("material_prototype.wrong_kind",
                                                         prototype.id.value() +
                                                             ": prototype kind must be material");
    }

    auto domain_text = required_field(prototype, "domain");
    if (!domain_text) {
        return core::Result<MaterialDefinition>::failure(domain_text.error().code,
                                                         domain_text.error().message);
    }
    auto domain = parse_material_domain(domain_text.value());
    if (!domain) {
        return core::Result<MaterialDefinition>::failure("material_prototype.invalid_domain",
                                                         prototype.id.value() +
                                                             ": unsupported material domain " +
                                                             std::string(domain_text.value()));
    }

    auto blend_mode_text = required_field(prototype, "blend_mode");
    if (!blend_mode_text) {
        return core::Result<MaterialDefinition>::failure(blend_mode_text.error().code,
                                                         blend_mode_text.error().message);
    }
    auto blend_mode = parse_material_blend_mode(blend_mode_text.value());
    if (!blend_mode) {
        return core::Result<MaterialDefinition>::failure("material_prototype.invalid_blend_mode",
                                                         prototype.id.value() +
                                                             ": unsupported material blend mode " +
                                                             std::string(blend_mode_text.value()));
    }

    auto shader_template_text = required_field(prototype, "shader_template");
    if (!shader_template_text) {
        return core::Result<MaterialDefinition>::failure(shader_template_text.error().code,
                                                         shader_template_text.error().message);
    }
    auto shader_template =
        parse_virtual_path(shader_template_text.value(), prototype, "shader_template",
                           "material_prototype.invalid_shader_template");
    if (!shader_template) {
        return core::Result<MaterialDefinition>::failure(shader_template.error().code,
                                                         shader_template.error().message);
    }

    MaterialDefinition definition;
    definition.id = prototype.id;
    definition.domain = domain.value();
    definition.blend_mode = blend_mode.value();
    definition.shader_template = shader_template.value();

    if (const auto* double_sided = field(prototype, "double_sided")) {
        auto parsed = parse_bool(*double_sided, prototype, "double_sided");
        if (!parsed) {
            return core::Result<MaterialDefinition>::failure(parsed.error().code,
                                                             parsed.error().message);
        }
        definition.double_sided = parsed.value();
    }

    for (const auto& key : sorted_field_keys(prototype)) {
        const auto* value = field(prototype, key);
        if (value == nullptr) {
            continue;
        }

        if (starts_with(key, texture_prefix)) {
            const auto name = std::string_view(key).substr(texture_prefix.size());
            auto texture =
                parse_virtual_path(*value, prototype, key, "material_prototype.invalid_texture");
            if (!texture) {
                return core::Result<MaterialDefinition>::failure(texture.error().code,
                                                                 texture.error().message);
            }
            definition.textures.push_back(
                MaterialTextureBinding{std::string(name), texture.value(), true});
        } else if (starts_with(key, optional_texture_prefix)) {
            const auto name = std::string_view(key).substr(optional_texture_prefix.size());
            auto texture =
                parse_virtual_path(*value, prototype, key, "material_prototype.invalid_texture");
            if (!texture) {
                return core::Result<MaterialDefinition>::failure(texture.error().code,
                                                                 texture.error().message);
            }
            definition.textures.push_back(
                MaterialTextureBinding{std::string(name), texture.value(), false});
        } else if (starts_with(key, scalar_prefix)) {
            float scalar = 0.0F;
            if (!parse_float(*value, scalar)) {
                return core::Result<MaterialDefinition>::failure(
                    "material_prototype.invalid_scalar",
                    prototype.id.value() + ": " + key + " must be a finite float");
            }
            definition.scalars.push_back(MaterialScalarParameter{
                std::string(std::string_view(key).substr(scalar_prefix.size())), scalar});
        } else if (starts_with(key, color_prefix)) {
            auto color = parse_color(*value, prototype, key);
            if (!color) {
                return core::Result<MaterialDefinition>::failure(color.error().code,
                                                                 color.error().message);
            }
            definition.colors.push_back(MaterialColorParameter{
                std::string(std::string_view(key).substr(color_prefix.size())), color.value()});
        } else if (!is_allowed_metadata_field(key)) {
            return core::Result<MaterialDefinition>::failure(
                "material_prototype.unknown_field",
                prototype.id.value() + ": unsupported material field " + key);
        }
    }

    auto validation = validate_material_definition(definition);
    if (!validation) {
        return core::Result<MaterialDefinition>::failure(
            validation.error().code, prototype.id.value() + ": " + validation.error().message);
    }

    return core::Result<MaterialDefinition>::success(std::move(definition));
}

core::Result<MaterialRegistry>
material_registry_from_prototypes(const modding::PrototypeRegistry& prototypes) {
    MaterialRegistry registry;
    for (const auto* prototype : prototypes.prototypes_of_kind(modding::PrototypeKinds::material)) {
        auto definition = material_definition_from_prototype(*prototype);
        if (!definition) {
            return core::Result<MaterialRegistry>::failure(definition.error().code,
                                                           definition.error().message);
        }
        auto status = registry.add(std::move(definition).value());
        if (!status) {
            return core::Result<MaterialRegistry>::failure(status.error().code,
                                                           status.error().message);
        }
    }
    return core::Result<MaterialRegistry>::success(std::move(registry));
}

} // namespace heartstead::renderer::materials
