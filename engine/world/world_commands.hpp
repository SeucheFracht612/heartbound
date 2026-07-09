#pragma once

#include "engine/core/result.hpp"
#include "engine/net/server_command.hpp"

namespace heartstead::world {

class WorldCommandRegistry {
  public:
    [[nodiscard]] static core::Status
    register_engine_commands(net::ServerCommandDispatcher& dispatcher);
};

} // namespace heartstead::world
