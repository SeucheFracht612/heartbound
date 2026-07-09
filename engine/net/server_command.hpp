#pragma once

#include "engine/core/ids.hpp"
#include "engine/core/result.hpp"
#include "engine/modding/prototype_registry.hpp"
#include "engine/save/save_metadata.hpp"
#include "engine/world/operations/world_operation.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace heartstead::world {
class WorldState;
}

namespace heartstead::net {

enum class CommandExecutorRole {
    client_prediction,
    authoritative_server,
};

struct CommandEnvelope {
    std::uint64_t sequence = 0;
    core::NetId sender;
    std::string type;
    std::string payload;
    std::int64_t client_time_ms = 0;
};

struct CommandExecutionContext {
    CommandExecutorRole executor_role = CommandExecutorRole::authoritative_server;
    std::int64_t server_time_ms = 0;
    save::SaveIdAllocator* save_ids = nullptr;
    const modding::PrototypeRegistry* prototypes = nullptr;
    heartstead::world::WorldState* world_state = nullptr;
};

struct CommandOperationTrace {
    std::vector<world::OperationStage> stages;
    std::vector<std::string> mutations;
    std::vector<std::string> derived_updates;
    bool replication_dirty = false;
    bool save_dirty = false;
};

struct CommandDispatchResult {
    std::uint64_t sequence = 0;
    std::string command_type;
    bool committed_world_mutation = false;
    std::vector<world::OperationEvent> events;
    std::vector<core::SaveId> reserved_ids;
    CommandOperationTrace operation_trace;
};

struct CommandDispatchReport {
    std::uint64_t sequence = 0;
    std::string command_type;
    bool succeeded = false;
    bool committed_world_mutation = false;
    std::vector<world::OperationEvent> events;
    std::vector<core::SaveId> reserved_ids;
    CommandOperationTrace operation_trace;
    std::optional<core::Error> error;
};

using CommandHandler = std::function<core::Status(
    const CommandEnvelope&, const CommandExecutionContext&, world::WorldOperation&)>;

struct CommandDescriptor {
    std::string type;
    bool mutates_world = true;
    bool requires_authoritative_server = true;
    CommandHandler handler;
};

class ServerCommandDispatcher {
  public:
    [[nodiscard]] core::Status register_command(CommandDescriptor descriptor);
    [[nodiscard]] core::Result<CommandDispatchResult>
    dispatch(const CommandEnvelope& envelope, const CommandExecutionContext& context) const;
    [[nodiscard]] CommandDispatchReport
    dispatch_report(const CommandEnvelope& envelope, const CommandExecutionContext& context) const;

    [[nodiscard]] bool contains(std::string_view type) const;
    [[nodiscard]] std::size_t size() const noexcept;

  private:
    std::unordered_map<std::string, CommandDescriptor> commands_;
};

} // namespace heartstead::net
