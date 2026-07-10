#include "engine/workpieces/pattern_library.hpp"

#include <algorithm>
#include <charconv>
#include <set>
#include <string>
#include <string_view>
#include <system_error>

namespace heartstead::workpieces {

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

[[nodiscard]] core::Result<std::uint16_t> u16(std::string_view value) {
    std::uint16_t parsed = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (error != std::errc{} || end != value.data() + value.size())
        return core::Result<std::uint16_t>::failure("pattern.invalid_number",
                                                    "pattern coordinate must be u16");
    return core::Result<std::uint16_t>::success(parsed);
}

[[nodiscard]] core::Result<bool> boolean(const modding::GenericPrototype& prototype,
                                         std::string_view key, bool fallback) {
    const auto* value = field(prototype, key);
    if (value == nullptr)
        return core::Result<bool>::success(fallback);
    if (*value == "true")
        return core::Result<bool>::success(true);
    if (*value == "false")
        return core::Result<bool>::success(false);
    return core::Result<bool>::failure("pattern.invalid_bool", std::string(key) + " must be bool");
}

[[nodiscard]] PatternVariant transform(const PatternDefinition& pattern, std::uint16_t rotation,
                                       bool mirrored) {
    PatternVariant result;
    result.rotation_degrees = rotation;
    result.mirrored = mirrored;
    const auto quarter_turn = rotation == 90 || rotation == 270;
    result.shape = quarter_turn ? WorkpieceGridShape{pattern.shape.depth, pattern.shape.height,
                                                     pattern.shape.width}
                                : pattern.shape;
    for (auto cell : pattern.occupied_cells) {
        if (mirrored)
            cell.x = static_cast<std::uint16_t>(pattern.shape.width - 1U - cell.x);
        WorkpieceCellCoord transformed = cell;
        if (rotation == 90)
            transformed = {static_cast<std::uint16_t>(pattern.shape.depth - 1U - cell.z), cell.y,
                           cell.x};
        else if (rotation == 180)
            transformed = {static_cast<std::uint16_t>(pattern.shape.width - 1U - cell.x), cell.y,
                           static_cast<std::uint16_t>(pattern.shape.depth - 1U - cell.z)};
        else if (rotation == 270)
            transformed = {cell.z, cell.y,
                           static_cast<std::uint16_t>(pattern.shape.width - 1U - cell.x)};
        result.occupied_cells.push_back(transformed);
    }
    std::ranges::sort(result.occupied_cells);
    return result;
}

} // namespace

core::Status PatternDefinition::validate() const {
    if (!prototype_id.is_valid() || !output_prototype_id.is_valid())
        return core::Status::failure("pattern.invalid_prototype",
                                     "pattern and output prototype ids are required");
    auto grid = WorkpieceGrid::create(shape);
    if (!grid)
        return core::Status::failure(grid.error().code, grid.error().message);
    if (occupied_cells.empty())
        return core::Status::failure("pattern.empty", "pattern requires occupied cells");
    std::set<WorkpieceCellCoord> unique;
    for (const auto cell : occupied_cells) {
        if (cell.x >= shape.width || cell.y >= shape.height || cell.z >= shape.depth)
            return core::Status::failure("pattern.cell_out_of_bounds",
                                         "pattern cell is outside shape");
        if (!unique.insert(cell).second)
            return core::Status::failure("pattern.duplicate_cell", "pattern cell is duplicated");
    }
    for (const auto& material : material_constraints)
        if (!material.is_valid())
            return core::Status::failure("pattern.invalid_material",
                                         "pattern material constraint is invalid");
    return core::Status::ok();
}

core::Status PatternLibrary::add(PatternDefinition definition) {
    auto status = definition.validate();
    if (!status)
        return status;
    if (by_id_.contains(definition.prototype_id.value()))
        return core::Status::failure("pattern.duplicate", "pattern prototype is duplicated");
    by_id_.emplace(definition.prototype_id.value(), patterns_.size());
    patterns_.push_back(std::move(definition));
    return core::Status::ok();
}

const PatternDefinition* PatternLibrary::find(const core::PrototypeId& id) const noexcept {
    const auto found = by_id_.find(id.value());
    return found == by_id_.end() ? nullptr : &patterns_[found->second];
}

std::vector<PatternVariant> PatternLibrary::variants(const core::PrototypeId& id) const {
    const auto* pattern = find(id);
    if (pattern == nullptr)
        return {};
    const std::vector<std::uint16_t> rotations = pattern->allow_rotate_y
                                                     ? std::vector<std::uint16_t>{0, 90, 180, 270}
                                                     : std::vector<std::uint16_t>{0};
    std::vector<PatternVariant> result;
    for (const auto rotation : rotations) {
        result.push_back(transform(*pattern, rotation, false));
        if (pattern->allow_mirror_x)
            result.push_back(transform(*pattern, rotation, true));
    }
    return result;
}

std::optional<core::PrototypeId> PatternLibrary::match(const WorkpieceGrid& grid,
                                                       const core::PrototypeId& material_id) const {
    for (const auto& pattern : patterns_) {
        if (!pattern.material_constraints.empty() &&
            std::ranges::find(pattern.material_constraints, material_id) ==
                pattern.material_constraints.end())
            continue;
        for (const auto& variant : variants(pattern.prototype_id)) {
            if (variant.shape != grid.shape())
                continue;
            bool matches = true;
            for (std::uint16_t z = 0; z < variant.shape.depth && matches; ++z)
                for (std::uint16_t y = 0; y < variant.shape.height && matches; ++y)
                    for (std::uint16_t x = 0; x < variant.shape.width && matches; ++x) {
                        const auto expected = std::ranges::binary_search(
                            variant.occupied_cells, WorkpieceCellCoord{x, y, z});
                        auto actual = grid.get({x, y, z});
                        matches = actual &&
                                  (expected ? actual.value().is_occupied()
                                            : (!pattern.strict || !actual.value().is_occupied()));
                    }
            if (matches)
                return pattern.prototype_id;
        }
    }
    return std::nullopt;
}

std::size_t PatternLibrary::size() const noexcept {
    return patterns_.size();
}

core::Result<PatternDefinition>
pattern_definition_from_prototype(const modding::GenericPrototype& prototype) {
    if (prototype.kind != modding::PrototypeKinds::pattern)
        return core::Result<PatternDefinition>::failure("pattern.invalid_kind",
                                                        "prototype kind must be pattern");
    const auto* shape_value = field(prototype, "shape");
    const auto* cells_value = field(prototype, "cells");
    const auto* output_value = field(prototype, "output");
    if (shape_value == nullptr || cells_value == nullptr || output_value == nullptr)
        return core::Result<PatternDefinition>::failure(
            "pattern.missing_field", "pattern requires shape, cells, and output");
    const auto shape_parts = split(*shape_value, '|');
    if (shape_parts.size() != 3)
        return core::Result<PatternDefinition>::failure("pattern.invalid_shape",
                                                        "pattern shape must be width|height|depth");
    auto width = u16(shape_parts[0]);
    auto height = u16(shape_parts[1]);
    auto depth = u16(shape_parts[2]);
    auto output = core::PrototypeId::parse(*output_value);
    auto rotate = boolean(prototype, "allow_rotate_y", true);
    auto mirror = boolean(prototype, "allow_mirror_x", false);
    auto strict = boolean(prototype, "strict", true);
    if (!width || !height || !depth || !output || !rotate || !mirror || !strict)
        return core::Result<PatternDefinition>::failure("pattern.invalid_fields",
                                                        "pattern fields are invalid");
    PatternDefinition pattern;
    pattern.prototype_id = prototype.id;
    pattern.shape = {width.value(), height.value(), depth.value()};
    pattern.output_prototype_id = *output;
    pattern.allow_rotate_y = rotate.value();
    pattern.allow_mirror_x = mirror.value();
    pattern.strict = strict.value();
    if (const auto* negative = field(prototype, "negative_mould"))
        pattern.negative_mould = *negative == "true";
    for (const auto encoded : split(*cells_value, ';')) {
        const auto parts = split(encoded, '|');
        if (parts.size() != 3)
            return core::Result<PatternDefinition>::failure("pattern.invalid_cells",
                                                            "pattern cell must be x|y|z");
        auto x = u16(parts[0]);
        auto y = u16(parts[1]);
        auto z = u16(parts[2]);
        if (!x || !y || !z)
            return core::Result<PatternDefinition>::failure("pattern.invalid_cells",
                                                            "pattern cell is invalid");
        pattern.occupied_cells.push_back({x.value(), y.value(), z.value()});
    }
    if (const auto* materials = field(prototype, "materials"))
        for (const auto value : split(*materials, ',')) {
            auto material = core::PrototypeId::parse(value);
            if (!material)
                return core::Result<PatternDefinition>::failure("pattern.invalid_material",
                                                                "pattern material is invalid");
            pattern.material_constraints.push_back(*material);
        }
    auto status = pattern.validate();
    if (!status)
        return core::Result<PatternDefinition>::failure(status.error().code,
                                                        status.error().message);
    return core::Result<PatternDefinition>::success(std::move(pattern));
}

core::Result<PatternLibrary>
pattern_library_from_prototypes(const modding::PrototypeRegistry& prototypes) {
    PatternLibrary library;
    auto patterns = prototypes.prototypes_of_kind(modding::PrototypeKinds::pattern);
    std::ranges::sort(patterns, {}, [](const auto* prototype) { return prototype->id.value(); });
    for (const auto* prototype : patterns) {
        auto definition = pattern_definition_from_prototype(*prototype);
        if (!definition)
            return core::Result<PatternLibrary>::failure(definition.error().code,
                                                         definition.error().message);
        auto status = library.add(std::move(definition).value());
        if (!status)
            return core::Result<PatternLibrary>::failure(status.error().code,
                                                         status.error().message);
    }
    return core::Result<PatternLibrary>::success(std::move(library));
}

} // namespace heartstead::workpieces
