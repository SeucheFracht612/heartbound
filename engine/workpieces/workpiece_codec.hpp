#pragma once

#include "engine/core/result.hpp"
#include "engine/workpieces/workpiece_grid.hpp"

#include <string>
#include <string_view>

namespace heartstead::workpieces {

class WorkpieceGridTextCodec {
  public:
    [[nodiscard]] static std::string encode(const WorkpieceGrid& grid);
    [[nodiscard]] static core::Result<WorkpieceGrid> decode(std::string_view text);
};

} // namespace heartstead::workpieces
