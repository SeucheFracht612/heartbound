#pragma once

#include "engine/core/result.hpp"
#include "engine/scripting/script_runtime.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace heartstead::scripting {

struct ScriptHostEvent {
    std::uint64_t sequence = 0;
    std::string api_id;
    std::string module_id;
    std::string source_mod_id;
    std::filesystem::path source_path;
    ScriptStage stage = ScriptStage::runtime_server;
    std::string function_name;
    std::uint32_t module_api_version = 1;
    std::uint32_t consumed_instruction_estimate = 0;
    std::vector<ScriptValue> arguments;
};

struct ScriptHostEventBatch {
    std::uint64_t first_sequence = 0;
    std::uint64_t last_sequence = 0;
    std::vector<ScriptHostEvent> events;
};

class ScriptHostEventQueue {
  public:
    explicit ScriptHostEventQueue(std::uint64_t next_sequence = 1);

    [[nodiscard]] std::uint64_t next_sequence() const noexcept;
    [[nodiscard]] std::size_t pending_count() const noexcept;
    [[nodiscard]] bool empty() const noexcept;

    [[nodiscard]] core::Result<ScriptHostEventBatch>
    enqueue_from_call(const ScriptModuleInfo& module, const ScriptCallDesc& call,
                      const ScriptCallResult& result, const ScriptRuntimeDesc& runtime_desc);

    [[nodiscard]] std::vector<ScriptHostEvent> drain();

  private:
    std::uint64_t next_sequence_ = 1;
    std::vector<ScriptHostEvent> pending_;
};

[[nodiscard]] core::Status validate_script_host_event(const ScriptHostEvent& event);
[[nodiscard]] core::Status validate_script_host_event_batch(const ScriptHostEventBatch& batch);

} // namespace heartstead::scripting
