#pragma once

#include "engine/core/ids.hpp"
#include "engine/core/result.hpp"
#include "engine/net/server_command.hpp"
#include "engine/scripting/script_host_event.hpp"
#include "engine/scripting/script_runtime.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace heartstead::game {

struct ScriptHostCommandRoute {
    std::string api_id;
    std::string command_type;
    heartstead::scripting::ScriptStage stage = heartstead::scripting::ScriptStage::runtime_server;
    std::vector<std::string> required_argument_keys;
    std::vector<std::string> optional_argument_keys;
};

struct ScriptHostCommandDispatchDesc {
    core::NetId sender;
    std::uint64_t first_sequence = 1;
    std::int64_t client_time_ms = 0;
};

struct ScriptHostCommandReport {
    std::uint64_t script_event_sequence = 0;
    std::uint64_t command_sequence = 0;
    std::string api_id;
    std::string command_type;
    bool success = false;
    bool committed_world_mutation = false;
    std::vector<heartstead::world::OperationEvent> events;
    std::vector<core::SaveId> reserved_ids;
    std::string error_code;
    std::string error_message;
};

struct ScriptHostCommandBatchResult {
    std::uint32_t command_count = 0;
    std::uint32_t success_count = 0;
    std::uint32_t failure_count = 0;
    std::vector<ScriptHostCommandReport> reports;
};

class ScriptHostCommandRouter {
  public:
    explicit ScriptHostCommandRouter(std::vector<ScriptHostCommandRoute> routes = {});

    [[nodiscard]] std::size_t route_count() const noexcept;
    [[nodiscard]] const ScriptHostCommandRoute* find_route(std::string_view api_id) const noexcept;
    [[nodiscard]] const std::vector<ScriptHostCommandRoute>& routes() const noexcept;

    [[nodiscard]] core::Result<std::vector<net::CommandEnvelope>>
    make_command_envelopes(const std::vector<scripting::ScriptHostEvent>& events,
                           ScriptHostCommandDispatchDesc desc) const;
    [[nodiscard]] core::Result<ScriptHostCommandBatchResult>
    dispatch_commands(const std::vector<scripting::ScriptHostEvent>& events,
                      ScriptHostCommandDispatchDesc desc,
                      const net::ServerCommandDispatcher& dispatcher,
                      const net::CommandExecutionContext& context) const;

  private:
    std::vector<ScriptHostCommandRoute> routes_;
};

[[nodiscard]] core::Status validate_script_host_command_route(const ScriptHostCommandRoute& route);
[[nodiscard]] core::Status
validate_script_host_command_routes(const std::vector<ScriptHostCommandRoute>& routes);
[[nodiscard]] core::Status
validate_script_host_command_report(const ScriptHostCommandReport& report);
[[nodiscard]] core::Status
validate_script_host_command_batch_result(const ScriptHostCommandBatchResult& batch);

} // namespace heartstead::game
