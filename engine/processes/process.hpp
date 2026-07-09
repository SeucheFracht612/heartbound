#pragma once

#include "engine/core/ids.hpp"
#include "engine/core/result.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace heartstead::processes {

struct ProcessSlot {
    core::PrototypeId prototype_id;
    std::uint32_t count = 0;

    [[nodiscard]] core::Status validate() const;
};

struct ProcessDefinition {
    core::PrototypeId prototype_id;
    std::int64_t default_required_work_ms = 0;
    bool requires_room = false;
    bool requires_power = false;
    std::uint32_t required_power_capacity = 1;
    std::int64_t base_quality_rate_per_mille = 1000;
    std::vector<std::string> tags;

    [[nodiscard]] core::Status validate() const;
};

enum class ProcessState {
    running,
    interrupted,
    complete,
};

struct ProcessModifiers {
    std::int64_t room_rate_per_mille = 1000;
    std::int64_t power_rate_per_mille = 1000;
    std::int64_t quality_rate_per_mille = 1000;

    [[nodiscard]] std::int64_t effective_rate_per_mille() const noexcept;
};

struct ProcessInstance {
    core::ProcessId process_id;
    core::SaveId owner_id;
    core::PrototypeId prototype_id;
    std::vector<ProcessSlot> input_slots;
    std::vector<ProcessSlot> output_slots;
    std::int64_t start_time_ms = 0;
    std::int64_t last_update_time_ms = 0;
    std::int64_t required_effective_work_ms = 0;
    std::int64_t accumulated_effective_work_ms = 0;
    ProcessState state = ProcessState::running;
    std::string interruption_reason;

    [[nodiscard]] core::Status validate() const;
    [[nodiscard]] bool is_complete() const noexcept;
    [[nodiscard]] std::int64_t remaining_effective_work_ms() const noexcept;
};

class ProcessRuntime {
  public:
    [[nodiscard]] static core::Result<ProcessInstance>
    create(core::ProcessId process_id, core::SaveId owner_id, core::PrototypeId prototype_id,
           std::int64_t now_ms, std::int64_t required_effective_work_ms);
    [[nodiscard]] static core::Result<ProcessInstance> create(core::ProcessId process_id,
                                                              core::SaveId owner_id,
                                                              const ProcessDefinition& definition,
                                                              std::int64_t now_ms);

    [[nodiscard]] static core::Status advance(ProcessInstance& instance, std::int64_t now_ms,
                                              ProcessModifiers modifiers);
    static void interrupt(ProcessInstance& instance, std::string reason);
    [[nodiscard]] static core::Status resume(ProcessInstance& instance, std::int64_t now_ms);
};

class ProcessIdAllocator {
  public:
    explicit ProcessIdAllocator(std::uint64_t next_value = 1);

    [[nodiscard]] core::Result<core::ProcessId> reserve();
    [[nodiscard]] core::ProcessId peek_next() const noexcept;

  private:
    std::uint64_t next_value_ = 1;
};

[[nodiscard]] bool is_known_process_state(ProcessState state) noexcept;

} // namespace heartstead::processes
