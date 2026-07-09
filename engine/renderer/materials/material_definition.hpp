#pragma once

#include "engine/assets/virtual_file_system.hpp"
#include "engine/core/ids.hpp"
#include "engine/core/result.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace heartstead::renderer::materials {

enum class MaterialDomain {
    surface,
    terrain,
    foliage,
    water,
    particle,
    ui,
};

enum class MaterialBlendMode {
    opaque,
    masked,
    translucent,
    additive,
};

struct MaterialColor {
    float red = 1.0F;
    float green = 1.0F;
    float blue = 1.0F;
    float alpha = 1.0F;
};

struct MaterialTextureBinding {
    std::string name;
    assets::VirtualPath texture;
    bool required = true;
};

struct MaterialScalarParameter {
    std::string name;
    float value = 0.0F;
};

struct MaterialColorParameter {
    std::string name;
    MaterialColor value{};
};

struct MaterialDefinition {
    core::PrototypeId id;
    MaterialDomain domain = MaterialDomain::surface;
    MaterialBlendMode blend_mode = MaterialBlendMode::opaque;
    assets::VirtualPath shader_template;
    bool double_sided = false;
    std::vector<MaterialTextureBinding> textures;
    std::vector<MaterialScalarParameter> scalars;
    std::vector<MaterialColorParameter> colors;
};

class MaterialRegistry {
  public:
    [[nodiscard]] core::Status add(MaterialDefinition definition);
    [[nodiscard]] const MaterialDefinition* find(std::string_view material_id) const noexcept;
    [[nodiscard]] std::vector<const MaterialDefinition*> definitions() const;
    [[nodiscard]] std::size_t count_domain(MaterialDomain domain) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] bool empty() const noexcept;

  private:
    std::vector<MaterialDefinition> definitions_;
};

[[nodiscard]] core::Status validate_material_definition(const MaterialDefinition& definition);
[[nodiscard]] std::optional<MaterialDomain> parse_material_domain(std::string_view value) noexcept;
[[nodiscard]] std::optional<MaterialBlendMode>
parse_material_blend_mode(std::string_view value) noexcept;
[[nodiscard]] std::string_view material_domain_name(MaterialDomain domain) noexcept;
[[nodiscard]] std::string_view material_blend_mode_name(MaterialBlendMode blend_mode) noexcept;

} // namespace heartstead::renderer::materials
