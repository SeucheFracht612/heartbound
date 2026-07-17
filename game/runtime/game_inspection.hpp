#pragma once

#include "engine/debug/inspection.hpp"
#include "game/framework/gameplay_module.hpp"
#include "game/presentation/client_presentation.hpp"
#include "game/runtime/runtime_session.hpp"
#include "game/runtime/script_host_commands.hpp"

namespace heartstead::game {

class GameInspector {
  public:
    [[nodiscard]] static debug::InspectionData inspect(const ScriptHostCommandRoute& route);
    [[nodiscard]] static debug::InspectionData inspect(const ScriptHostCommandReport& report);
    [[nodiscard]] static debug::InspectionData inspect(const ScriptHostCommandBatchResult& batch);
    [[nodiscard]] static debug::InspectionData
    inspect(const GameplayModuleRegistrationReport& report);
    [[nodiscard]] static debug::InspectionData inspect(const ClientRuntimeStats& stats);
    [[nodiscard]] static debug::InspectionData
    inspect(const PresentationSynchronizationStats& stats);
    [[nodiscard]] static debug::InspectionData inspect(const RuntimeFrameStats& stats);
    [[nodiscard]] static debug::InspectionData inspect(const RuntimeSession& session);
    [[nodiscard]] static std::vector<debug::InspectionData>
    inspect_system_timings(const ServerRuntime& server);
};

} // namespace heartstead::game
