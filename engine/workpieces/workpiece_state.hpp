#pragma once

#include "engine/core/ids.hpp"
#include "engine/core/result.hpp"
#include "engine/workpieces/workpiece_grid.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace heartstead::workpieces {

struct WorkpieceOutputMetadata {
    std::string classification;
    std::uint64_t mass_units = 0;
    bool base_closed = false;
    bool thin_walls = false;
    std::map<std::string, std::int64_t> measurements;
};

struct WorkpieceServerState {
    std::vector<std::uint64_t> blob_mask;
    std::vector<std::uint64_t> hidden_flaw_mask;
    std::vector<std::uint64_t> revealed_mask;
    WorkpieceOutputMetadata output_metadata;

    [[nodiscard]] static core::Result<WorkpieceServerState>
    generate(WorkpieceGridShape shape, std::uint64_t seed,
             std::uint16_t flaw_chance_per_mille = 50);
    [[nodiscard]] core::Status validate(WorkpieceGridShape shape) const;
    [[nodiscard]] bool in_blob(std::size_t cell_index) const noexcept;
    [[nodiscard]] bool flaw_revealed(std::size_t cell_index) const noexcept;
    [[nodiscard]] core::Result<bool> reveal(std::size_t cell_index);
};

struct WorkpiecePlanningState {
    std::vector<std::uint64_t> planned_cells;
    std::uint64_t revision = 0;
};

struct WorkpieceFinishResult {
    core::PrototypeId pattern_id;
    core::PrototypeId output_prototype_id;
    WorkpieceOutputMetadata metadata;
    std::uint32_t byproduct_units = 0;
};

class PatternLibrary;

[[nodiscard]] core::Result<WorkpieceFinishResult>
finish_workpiece(const WorkpieceGrid& grid, const WorkpieceServerState* server_state,
                 const PatternLibrary& patterns, const core::PrototypeId& material_id,
                 const std::optional<core::PrototypeId>& requested_pattern = std::nullopt);

class WorkpieceServerStateTextCodec {
  public:
    [[nodiscard]] static std::string encode(const WorkpieceServerState& state,
                                            WorkpieceGridShape shape);
    [[nodiscard]] static core::Result<WorkpieceServerState> decode(std::string_view text,
                                                                   WorkpieceGridShape shape);
};

} // namespace heartstead::workpieces
