#include "engine/workpieces/workpiece_prototype.hpp"

#include "engine/modding/prototype_registry.hpp"

#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace heartstead::workpieces {

namespace {

[[nodiscard]] const std::string* field(const modding::GenericPrototype& prototype,
                                       std::string_view key) {
    const auto found = prototype.fields.find(std::string(key));
    return found == prototype.fields.end() ? nullptr : &found->second;
}

[[nodiscard]] core::Result<std::uint16_t> parse_axis(std::string_view value,
                                                     std::string_view axis_name) {
    std::uint64_t parsed = 0;
    const auto* begin = value.data();
    const auto* end = value.data() + value.size();
    const auto [ptr, error] = std::from_chars(begin, end, parsed);
    if (error != std::errc{} || ptr != end || parsed == 0 || parsed > 64) {
        return core::Result<std::uint16_t>::failure("workpiece_prototype.invalid_grid",
                                                    std::string(axis_name) +
                                                        " grid axis must be between 1 and 64");
    }
    return core::Result<std::uint16_t>::success(static_cast<std::uint16_t>(parsed));
}

[[nodiscard]] core::Result<WorkpieceGridShape> parse_grid_shape(std::string_view value) {
    const auto first_x = value.find('x');
    const auto second_x =
        first_x == std::string_view::npos ? std::string_view::npos : value.find('x', first_x + 1);
    if (first_x == std::string_view::npos || second_x == std::string_view::npos) {
        return core::Result<WorkpieceGridShape>::failure(
            "workpiece_prototype.invalid_grid", "workpiece grid must be formatted as WxHxD");
    }

    auto width = parse_axis(value.substr(0, first_x), "width");
    auto height = parse_axis(value.substr(first_x + 1, second_x - first_x - 1), "height");
    auto depth = parse_axis(value.substr(second_x + 1), "depth");
    if (!width) {
        return core::Result<WorkpieceGridShape>::failure(width.error().code, width.error().message);
    }
    if (!height) {
        return core::Result<WorkpieceGridShape>::failure(height.error().code,
                                                         height.error().message);
    }
    if (!depth) {
        return core::Result<WorkpieceGridShape>::failure(depth.error().code, depth.error().message);
    }
    return core::Result<WorkpieceGridShape>::success(
        {width.value(), height.value(), depth.value()});
}

} // namespace

core::Result<WorkpieceDefinition>
workpiece_definition_from_prototype(const modding::GenericPrototype& prototype) {
    if (prototype.kind != modding::PrototypeKinds::workpiece) {
        return core::Result<WorkpieceDefinition>::failure("workpiece_prototype.kind_mismatch",
                                                          "prototype is not a workpiece");
    }
    if (!prototype.id.is_valid()) {
        return core::Result<WorkpieceDefinition>::failure("workpiece_prototype.invalid_id",
                                                          "workpiece prototype id is invalid");
    }

    const auto* grid_value = field(prototype, "grid");
    if (grid_value == nullptr || grid_value->empty()) {
        return core::Result<WorkpieceDefinition>::failure("workpiece_prototype.missing_grid",
                                                          "workpiece prototype must declare grid");
    }
    const auto* material_value = field(prototype, "material");
    if (material_value == nullptr || material_value->empty()) {
        return core::Result<WorkpieceDefinition>::failure(
            "workpiece_prototype.missing_material", "workpiece prototype must declare material");
    }

    auto grid_shape = parse_grid_shape(*grid_value);
    auto material = core::PrototypeId::parse(*material_value);
    if (!grid_shape) {
        return core::Result<WorkpieceDefinition>::failure(grid_shape.error().code,
                                                          grid_shape.error().message);
    }
    if (!material) {
        return core::Result<WorkpieceDefinition>::failure(
            "workpiece_prototype.invalid_material", "workpiece material must be a prototype id");
    }

    WorkpieceDefinition definition;
    definition.prototype_id = prototype.id;
    definition.material_prototype_id = std::move(material).value();
    definition.grid_shape = grid_shape.value();

    auto status = definition.validate();
    if (!status) {
        return core::Result<WorkpieceDefinition>::failure(status.error().code,
                                                          status.error().message);
    }
    return core::Result<WorkpieceDefinition>::success(std::move(definition));
}

} // namespace heartstead::workpieces
