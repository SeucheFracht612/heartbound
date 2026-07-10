#pragma once

#include "engine/core/ids.hpp"
#include "engine/core/result.hpp"
#include "engine/simulation/world_time.hpp"

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
    union {
        simulation::WorldTick default_required_work_ticks = 0;
        simulation::WorldTick default_required_work_ms;
    };
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

enum class ProcessInterruptionPolicy {
    pause,
    reset,
    fail,
};

enum class ProcessEvaluationTrigger {
    chunk_load,
    interaction,
    render_proximity,
    state_change,
    inspection,
    save_load_validation,
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
    union {
        simulation::WorldTick started_at = 0;
        simulation::WorldTick start_time_ms;
    };
    union {
        simulation::WorldTick last_eval = 0;
        simulation::WorldTick last_update_time_ms;
    };
    union {
        simulation::WorldTick required_work_ticks = 0;
        simulation::WorldTick required_effective_work_ms;
    };
    union {
        simulation::WorldTick accrued_work_ticks = 0;
        simulation::WorldTick accumulated_effective_work_ms;
    };
    ProcessState state = ProcessState::running;
    std::string interruption_reason;
    bool output_claimed = false;
    std::string condition_function_id;
    ProcessInterruptionPolicy interruption_policy = ProcessInterruptionPolicy::pause;

    [[nodiscard]] core::Status validate() const;
    [[nodiscard]] bool is_complete() const noexcept;
    [[nodiscard]] simulation::WorldTick remaining_work_ticks() const noexcept;
    [[nodiscard]] simulation::WorldTick remaining_effective_work_ms() const noexcept {
        return remaining_work_ticks();
    }
};

class ProcessRuntime {
  public:
    [[nodiscard]] static core::Result<ProcessInstance>
    create(core::ProcessId process_id, core::SaveId owner_id, core::PrototypeId prototype_id,
           simulation::WorldTick world_time, simulation::WorldTick required_work_ticks);
    [[nodiscard]] static core::Result<ProcessInstance> create(core::ProcessId process_id,
                                                              core::SaveId owner_id,
                                                              const ProcessDefinition& definition,
                                                              simulation::WorldTick world_time);

    [[nodiscard]] static core::Status advance(ProcessInstance& instance,
                                              simulation::WorldTick world_time,
                                              ProcessModifiers modifiers);
    [[nodiscard]] static core::Status evaluate(ProcessInstance& instance,
                                               simulation::WorldTick world_time,
                                               ProcessModifiers modifiers,
                                               ProcessEvaluationTrigger trigger);
    static void interrupt(ProcessInstance& instance, std::string reason);
    [[nodiscard]] static core::Status resume(ProcessInstance& instance,
                                             simulation::WorldTick world_time);
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
[[nodiscard]] bool is_known_interruption_policy(ProcessInterruptionPolicy policy) noexcept;

} // namespace heartstead::processes
