#include "engine/processes/process.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace heartstead::processes {

namespace {

[[nodiscard]] std::int64_t sanitized_rate(std::int64_t rate) noexcept {
    return std::clamp(rate, std::int64_t{0}, std::int64_t{10000});
}

[[nodiscard]] std::int64_t scaled_delta(std::int64_t delta_ms,
                                        std::int64_t rate_per_mille) noexcept {
    if (delta_ms <= 0 || rate_per_mille <= 0) {
        return 0;
    }

    constexpr auto max = std::numeric_limits<std::int64_t>::max();
    if (delta_ms > max / rate_per_mille) {
        return max;
    }

    return (delta_ms * rate_per_mille) / 1000;
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
    if (default_required_work_ms <= 0) {
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
    if (required_effective_work_ms <= 0) {
        return core::Status::failure("process.invalid_required_work",
                                     "required process work must be positive");
    }
    if (accumulated_effective_work_ms < 0 ||
        accumulated_effective_work_ms > required_effective_work_ms) {
        return core::Status::failure("process.invalid_accumulated_work",
                                     "process accumulated work is outside the valid range");
    }
    if (last_update_time_ms < start_time_ms) {
        return core::Status::failure("process.invalid_time",
                                     "process last update time cannot predate start time");
    }
    if (!is_known_process_state(state)) {
        return core::Status::failure("process.invalid_state", "process state is unknown");
    }
    if (state == ProcessState::complete &&
        accumulated_effective_work_ms != required_effective_work_ms) {
        return core::Status::failure("process.invalid_complete_work",
                                     "complete processes must have all required work accumulated");
    }
    if (state != ProcessState::complete &&
        accumulated_effective_work_ms == required_effective_work_ms) {
        return core::Status::failure("process.invalid_incomplete_work",
                                     "processes with all work accumulated must be complete");
    }
    if (state != ProcessState::interrupted && !interruption_reason.empty()) {
        return core::Status::failure("process.invalid_interruption_reason",
                                     "only interrupted processes may keep an interruption reason");
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

std::int64_t ProcessInstance::remaining_effective_work_ms() const noexcept {
    return std::max(std::int64_t{0}, required_effective_work_ms - accumulated_effective_work_ms);
}

core::Result<ProcessInstance> ProcessRuntime::create(core::ProcessId process_id,
                                                     core::SaveId owner_id,
                                                     core::PrototypeId prototype_id,
                                                     std::int64_t now_ms,
                                                     std::int64_t required_effective_work_ms) {
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
    if (required_effective_work_ms <= 0) {
        return core::Result<ProcessInstance>::failure("process.invalid_required_work",
                                                      "required process work must be positive");
    }

    ProcessInstance instance;
    instance.process_id = process_id;
    instance.owner_id = owner_id;
    instance.prototype_id = std::move(prototype_id);
    instance.start_time_ms = now_ms;
    instance.last_update_time_ms = now_ms;
    instance.required_effective_work_ms = required_effective_work_ms;
    auto status = instance.validate();
    if (!status) {
        return core::Result<ProcessInstance>::failure(status.error().code, status.error().message);
    }
    return core::Result<ProcessInstance>::success(std::move(instance));
}

core::Result<ProcessInstance> ProcessRuntime::create(core::ProcessId process_id,
                                                     core::SaveId owner_id,
                                                     const ProcessDefinition& definition,
                                                     std::int64_t now_ms) {
    auto status = definition.validate();
    if (!status) {
        return core::Result<ProcessInstance>::failure(status.error().code, status.error().message);
    }
    return create(process_id, owner_id, definition.prototype_id, now_ms,
                  definition.default_required_work_ms);
}

core::Status ProcessRuntime::advance(ProcessInstance& instance, std::int64_t now_ms,
                                     ProcessModifiers modifiers) {
    if (instance.state == ProcessState::complete || instance.state == ProcessState::interrupted) {
        return core::Status::ok();
    }

    if (now_ms < instance.last_update_time_ms) {
        return core::Status::failure("process.time_reversed", "process time cannot move backward");
    }

    const auto delta_ms = now_ms - instance.last_update_time_ms;
    const auto effective_delta = scaled_delta(delta_ms, modifiers.effective_rate_per_mille());
    const auto applied_delta = std::min(instance.remaining_effective_work_ms(), effective_delta);
    instance.accumulated_effective_work_ms += applied_delta;
    instance.last_update_time_ms = now_ms;

    if (instance.accumulated_effective_work_ms >= instance.required_effective_work_ms) {
        instance.state = ProcessState::complete;
    }

    return core::Status::ok();
}

void ProcessRuntime::interrupt(ProcessInstance& instance, std::string reason) {
    if (instance.state == ProcessState::complete) {
        return;
    }
    instance.state = ProcessState::interrupted;
    instance.interruption_reason = std::move(reason);
}

core::Status ProcessRuntime::resume(ProcessInstance& instance, std::int64_t now_ms) {
    if (now_ms < instance.last_update_time_ms) {
        return core::Status::failure("process.time_reversed", "process time cannot move backward");
    }
    if (instance.state == ProcessState::interrupted) {
        instance.state = ProcessState::running;
        instance.interruption_reason.clear();
        instance.last_update_time_ms = now_ms;
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

} // namespace heartstead::processes
