#pragma once

#include "engine/debug/inspection.hpp"
#include "game/runtime/script_host_commands.hpp"

namespace heartstead::game {

class GameInspector {
  public:
    [[nodiscard]] static debug::InspectionData inspect(const ScriptHostCommandRoute& route);
    [[nodiscard]] static debug::InspectionData inspect(const ScriptHostCommandReport& report);
    [[nodiscard]] static debug::InspectionData inspect(const ScriptHostCommandBatchResult& batch);
};

} // namespace heartstead::game
