#pragma once

#include "engine/core/result.hpp"
#include "engine/net/server_command.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace heartstead::replay {

inline constexpr std::uint32_t current_command_replay_version = 1;

struct CommandReplayExpectation {
    bool has_committed_world_mutation = false;
    bool committed_world_mutation = false;
    std::optional<std::size_t> event_count;
    std::optional<std::size_t> reserved_id_count;
    std::vector<std::string> event_types;
    std::vector<core::SaveId> reserved_ids;
    std::vector<std::string> mutations;
    std::vector<std::string> derived_updates;
    std::optional<bool> replication_dirty;
    std::optional<bool> save_dirty;
    std::optional<world::OperationStage> last_stage;
    std::optional<std::string> error_code;
    std::optional<std::string> error_message;

    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] core::Status validate() const;
};

struct RecordedCommand {
    net::CommandEnvelope envelope;
    std::int64_t server_time_ms = 0;
    CommandReplayExpectation expectation;
};

struct CommandReplayLog {
    std::uint32_t version = current_command_replay_version;
    std::string scenario_id;
    std::uint64_t world_seed = 0;
    std::vector<RecordedCommand> commands;

    [[nodiscard]] core::Status validate() const;
};

struct CommandReplayStep {
    std::size_t index = 0;
    std::uint64_t sequence = 0;
    std::string command_type;
    bool success = false;
    bool committed_world_mutation = false;
    std::vector<world::OperationEvent> events;
    std::vector<core::SaveId> reserved_ids;
    net::CommandOperationTrace operation_trace;
    std::string error_code;
    std::string error_message;
    bool expectation_checked = false;
};

struct CommandReplayReport {
    std::vector<CommandReplayStep> steps;
};

class CommandReplayCodec {
  public:
    [[nodiscard]] static std::string encode(const CommandReplayLog& log);
    [[nodiscard]] static core::Result<CommandReplayLog> decode(std::string_view text);
};

class CommandReplayRunner {
  public:
    [[nodiscard]] static core::Result<CommandReplayReport>
    run(const CommandReplayLog& log, const net::ServerCommandDispatcher& dispatcher,
        net::CommandExecutionContext context);
};

} // namespace heartstead::replay
