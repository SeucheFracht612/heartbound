#pragma once

#include "engine/core/ids.hpp"
#include "engine/core/result.hpp"
#include "engine/modding/prototype_registry.hpp"
#include "engine/workpieces/workpiece_grid.hpp"

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace heartstead::workpieces {

struct PatternDefinition {
    core::PrototypeId prototype_id;
    WorkpieceGridShape shape;
    std::vector<WorkpieceCellCoord> occupied_cells;
    bool negative_mould = false;
    bool allow_rotate_y = true;
    bool allow_mirror_x = false;
    bool strict = true;
    std::vector<core::PrototypeId> material_constraints;
    core::PrototypeId output_prototype_id;

    [[nodiscard]] core::Status validate() const;
};

struct PatternVariant {
    WorkpieceGridShape shape;
    std::vector<WorkpieceCellCoord> occupied_cells;
    std::uint16_t rotation_degrees = 0;
    bool mirrored = false;
};

class PatternLibrary {
  public:
    [[nodiscard]] core::Status add(PatternDefinition definition);
    [[nodiscard]] const PatternDefinition* find(const core::PrototypeId& id) const noexcept;
    [[nodiscard]] std::vector<PatternVariant> variants(const core::PrototypeId& id) const;
    [[nodiscard]] std::optional<core::PrototypeId>
    match(const WorkpieceGrid& grid, const core::PrototypeId& material_id) const;
    [[nodiscard]] std::size_t size() const noexcept;

  private:
    std::vector<PatternDefinition> patterns_;
    std::unordered_map<std::string, std::size_t> by_id_;
};

[[nodiscard]] core::Result<PatternDefinition>
pattern_definition_from_prototype(const modding::GenericPrototype& prototype);
[[nodiscard]] core::Result<PatternLibrary>
pattern_library_from_prototypes(const modding::PrototypeRegistry& prototypes);

} // namespace heartstead::workpieces
