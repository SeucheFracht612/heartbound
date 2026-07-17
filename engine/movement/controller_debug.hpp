#pragma once

#include "engine/debug/debug_overlay.hpp"
#include "engine/movement/player_controller.hpp"

namespace heartstead::movement {

[[nodiscard]] core::Status submit_player_controller_debug(
    debug::DebugOverlayRegistry& overlays, const PlayerController& controller,
    const PlayerControllerState& state, double prediction_error = 0.0);

} // namespace heartstead::movement
