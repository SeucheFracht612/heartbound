#include "engine/processes/process.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace heartstead::processes {

namespace {

[[nodiscard]] std::int64_t sanitized_rate(std::int64_t rate) noexcept {
    return std::clamp(rate, std::int64_t{0}, std::int64_t{10000});
}

[[nodiscard]] simulation::WorldTick scaled_delta(simulation::WorldTick delta_ticks,
                                                 std::int64_t rate_per_mille) noexcept {
    if (delta_ticks == 0 || rate_per_mille <= 0) {
        return 0;
    }

    constexpr auto max = std::numeric_limits<simulation::WorldTick>::max();
    const auto rate = static_cast<simulation::WorldTick>(rate_per_mille);
    if (delta_ticks > max / rate) {
        return max;
    }

    return (delta_ticks * rate) / 1000U;
}

} // namespace

core::Status ProcessSlot::validate() const {
    if (!prototype_id.is_valid()) {
        return core::Status::failure("process_slot.invalid_prototype",
                                     "process slot prototype id must be valid");
    }
    if (count == 0) {
        return core::Status::failure("process_slot.invalid_count",
                                     "process slot count must be non-zero");
    }
    return core::Status::ok();
}

core::Status ProcessDefinition::validate() const {
    if (!prototype_id.is_valid()) {
        return core::Status::failure("process_definition.invalid_prototype",
                                     "process definition prototype id must be valid");
    }
    if (default_required_work_ticks == 0) {
        return core::Status::failure("process_definition.invalid_required_work",
                                     "default process work must be positive");
    }
    if (requires_power && required_power_capacity == 0) {
        return core::Status::failure(
            "process_definition.invalid_power_requirement",
            "required power capacity must be non-zero when power is required");
    }
    if (base_quality_rate_per_mille < 0 || base_quality_rate_per_mille > 10000) {
        return core::Status::failure("process_definition.invalid_quality_rate",
                                     "base quality rate must be between 0 and 10000 per-mille");
    }
    for (const auto& tag : tags) {
        if (!core::is_valid_local_id(tag)) {
            return core::Status::failure("process_definition.invalid_tag",
                                         "process definition tag is invalid: " + tag);
        }
    }
    return core::Status::ok();
}

std::int64_t ProcessModifiers::effective_rate_per_mille() const noexcept {
    const auto room = sanitized_rate(room_rate_per_mille);
    const auto power = sanitized_rate(power_rate_per_mille);
    const auto quality = sanitized_rate(quality_rate_per_mille);
    return (((room * power) / 1000) * quality) / 1000;
}

core::Status ProcessInstance::validate() const {
    if (!process_id.is_valid()) {
        return core::Status::failure("process.invalid_id", "process id must be valid");
    }
    if (!owner_id.is_valid()) {
        return core::Status::failure("process.invalid_owner",
                                     "process owner save id must be valid");
    }
    if (!prototype_id.is_valid()) {
        return core::Status::failure("process.invalid_prototype",
                                     "process prototype id must be valid");
    }
    if (required_work_ticks == 0) {
        return core::Status::failure("process.invalid_required_work",
                                     "required process work must be positive");
    }
    if (accrued_work_ticks > required_work_ticks) {
        return core::Status::failure("process.invalid_accumulated_work",
                                     "process accumulated work is outside the valid range");
    }
    if (last_eval < started_at) {
        return core::Status::failure("process.invalid_time",
                                     "process last update time cannot predate start time");
    }
    if (!is_known_process_state(state)) {
        return core::Status::failure("process.invalid_state", "process state is unknown");
    }
    if (state == ProcessState::complete && accrued_work_ticks != required_work_ticks) {
        return core::Status::failure("process.invalid_complete_work",
                                     "complete processes must have all required work accumulated");
    }
    if (state != ProcessState::complete && accrued_work_ticks == required_work_ticks) {
        return core::Status::failure("process.invalid_incomplete_work",
                                     "processes with all work accumulated must be complete");
    }
    if (state != ProcessState::interrupted && !interruption_reason.empty()) {
        return core::Status::failure("process.invalid_interruption_reason",
                                     "only interrupted processes may keep an interruption reason");
    }
    if (!is_known_interruption_policy(interruption_policy)) {
        return core::Status::failure("process.invalid_interruption_policy",
                                     "process interruption policy is unknown");
    }
    if (!condition_function_id.empty() && !core::is_valid_local_id(condition_function_id)) {
        return core::Status::failure("process.invalid_condition_function",
                                     "process condition function id is invalid");
    }
    if (output_claimed && state != ProcessState::complete) {
        return core::Status::failure("process.invalid_output_claim",
                                     "only a complete process may have claimed output");
    }
    for (const auto& slot : input_slots) {
        auto status = slot.validate();
        if (!status) {
            return status;
        }
    }
    for (const auto& slot : output_slots) {
        auto status = slot.validate();
        if (!status) {
            return status;
        }
    }
    return core::Status::ok();
}

bool ProcessInstance::is_complete() const noexcept {
    return state == ProcessState::complete;
}

simulation::WorldTick ProcessInstance::remaining_work_ticks() const noexcept {
    return accrued_work_ticks >= required_work_ticks ? 0 : required_work_ticks - accrued_work_ticks;
}

core::Result<ProcessInstance> ProcessRuntime::create(core::ProcessId process_id,
                                                     core::SaveId owner_id,
                                                     core::PrototypeId prototype_id,
                                                     simulation::WorldTick world_time,
                                                     simulation::WorldTick required_work_ticks) {
    if (!process_id.is_valid()) {
        return core::Result<ProcessInstance>::failure("process.invalid_id",
                                                      "process id must be valid");
    }
    if (!owner_id.is_valid()) {
        return core::Result<ProcessInstance>::failure("process.invalid_owner",
                                                      "process owner save id must be valid");
    }
    if (!prototype_id.is_valid()) {
        return core::Result<ProcessInstance>::failure("process.invalid_prototype",
                                                      "process prototype id must be valid");
    }
    if (required_work_ticks == 0) {
        return core::Result<ProcessInstance>::failure("process.invalid_required_work",
                                                      "required process work must be positive");
    }

    ProcessInstance instance;
    instance.process_id = process_id;
    instance.owner_id = owner_id;
    instance.prototype_id = std::move(prototype_id);
    instance.started_at = world_time;
    instance.last_eval = world_time;
    instance.required_work_ticks = required_work_ticks;
    auto status = instance.validate();
    if (!status) {
        return core::Result<ProcessInstance>::failure(status.error().code, status.error().message);
    }
    return core::Result<ProcessInstance>::success(std::move(instance));
}

core::Result<ProcessInstance> ProcessRuntime::create(core::ProcessId process_id,
                                                     core::SaveId owner_id,
                                                     const ProcessDefinition& definition,
                                                     simulation::WorldTick world_time) {
    auto status = definition.validate();
    if (!status) {
        return core::Result<ProcessInstance>::failure(status.error().code, status.error().message);
    }
    return create(process_id, owner_id, definition.prototype_id, world_time,
                  definition.default_required_work_ticks);
}

core::Status ProcessRuntime::advance(ProcessInstance& instance, simulation::WorldTick world_time,
                                     ProcessModifiers modifiers) {
    if (instance.state == ProcessState::complete || instance.state == ProcessState::interrupted) {
        return core::Status::ok();
    }

    if (world_time < instance.last_eval) {
        return core::Status::failure("process.time_reversed", "process time cannot move backward");
    }

    const auto delta_ticks = world_time - instance.last_eval;
    const auto effective_delta = scaled_delta(delta_ticks, modifiers.effective_rate_per_mille());
    const auto applied_delta = std::min(instance.remaining_work_ticks(), effective_delta);
    instance.accrued_work_ticks += applied_delta;
    instance.last_eval = world_time;

    if (instance.accrued_work_ticks >= instance.required_work_ticks) {
        instance.state = ProcessState::complete;
    }

    return core::Status::ok();
}

core::Status ProcessRuntime::evaluate(ProcessInstance& instance, simulation::WorldTick world_time,
                                      ProcessModifiers modifiers,
                                      ProcessEvaluationTrigger trigger) {
    (void)trigger;
    return advance(instance, world_time, modifiers);
}

void ProcessRuntime::interrupt(ProcessInstance& instance, std::string reason) {
    if (instance.state == ProcessState::complete) {
        return;
    }
    if (instance.interruption_policy == ProcessInterruptionPolicy::reset) {
        instance.accrued_work_ticks = 0;
    }
    instance.state = ProcessState::interrupted;
    instance.interruption_reason = std::move(reason);
}

core::Status ProcessRuntime::resume(ProcessInstance& instance, simulation::WorldTick world_time) {
    if (world_time < instance.last_eval) {
        return core::Status::failure("process.time_reversed", "process time cannot move backward");
    }
    if (instance.state == ProcessState::interrupted) {
        instance.state = ProcessState::running;
        instance.interruption_reason.clear();
        instance.last_eval = world_time;
    }
    return core::Status::ok();
}

ProcessIdAllocator::ProcessIdAllocator(std::uint64_t next_value)
    : next_value_(next_value == 0 ? 1 : next_value) {}

core::Result<core::ProcessId> ProcessIdAllocator::reserve() {
    if (next_value_ == 0) {
        return core::Result<core::ProcessId>::failure(
            "process.id_exhausted", "process id allocator exhausted its id range");
    }

    const auto id = core::ProcessId::from_value(next_value_);
    ++next_value_;
    return core::Result<core::ProcessId>::success(id);
}

core::ProcessId ProcessIdAllocator::peek_next() const noexcept {
    return core::ProcessId::from_value(next_value_);
}

bool is_known_process_state(ProcessState state) noexcept {
    switch (state) {
    case ProcessState::running:
    case ProcessState::interrupted:
    case ProcessState::complete:
        return true;
    }
    return false;
}

bool is_known_interruption_policy(ProcessInterruptionPolicy policy) noexcept {
    switch (policy) {
    case ProcessInterruptionPolicy::pause:
    case ProcessInterruptionPolicy::reset:
    case ProcessInterruptionPolicy::fail:
        return true;
    }
    return false;
}

} // namespace heartstead::processes
