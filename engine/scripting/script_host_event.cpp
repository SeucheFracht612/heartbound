#include "engine/scripting/script_host_event.hpp"

#include "engine/core/ids.hpp"

#include <limits>
#include <utility>

namespace heartstead::scripting {

namespace {

[[nodiscard]] bool is_valid_script_function_name(std::string_view value) noexcept {
    if (value.empty()) {
        return false;
    }

    for (const auto character : value) {
        const auto valid =
            (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') || character == '_' || character == '.';
        if (!valid) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] core::Result<ScriptHostEventBatch> make_batch(std::uint64_t first_sequence,
                                                            const ScriptModuleInfo& module,
                                                            const ScriptCallDesc& call,
                                                            const ScriptCallResult& result) {
    ScriptHostEventBatch batch;
    if (result.emitted_events.empty()) {
        return core::Result<ScriptHostEventBatch>::success(std::move(batch));
    }
    if (result.emitted_events.size() >
        std::numeric_limits<std::uint64_t>::max() - first_sequence + 1) {
        return core::Result<ScriptHostEventBatch>::failure(
            "scripting.host_event_sequence_overflow",
            "script host event sequence range exceeds uint64 capacity");
    }

    batch.first_sequence = first_sequence;
    batch.last_sequence = first_sequence + result.emitted_events.size() - 1;

    std::uint64_t sequence = first_sequence;
    for (const auto& emitted_event : result.emitted_events) {
        ScriptHostEvent event;
        event.sequence = sequence++;
        event.api_id = emitted_event.api_id;
        event.module_id = module.module_id;
        event.source_mod_id = module.source_mod_id;
        event.source_path = module.source_path;
        event.stage = module.stage;
        event.function_name = call.function_name;
        event.module_api_version = module.api_version;
        event.consumed_instruction_estimate = result.consumed_instruction_estimate;
        event.arguments = emitted_event.arguments;

        auto status = validate_script_host_event(event);
        if (!status) {
            return core::Result<ScriptHostEventBatch>::failure(status.error().code,
                                                               status.error().message);
        }
        batch.events.push_back(std::move(event));
    }

    return core::Result<ScriptHostEventBatch>::success(std::move(batch));
}

} // namespace

ScriptHostEventQueue::ScriptHostEventQueue(std::uint64_t next_sequence)
    : next_sequence_(next_sequence) {
    if (next_sequence_ == 0) {
        next_sequence_ = 1;
    }
}

std::uint64_t ScriptHostEventQueue::next_sequence() const noexcept {
    return next_sequence_;
}

std::size_t ScriptHostEventQueue::pending_count() const noexcept {
    return pending_.size();
}

bool ScriptHostEventQueue::empty() const noexcept {
    return pending_.empty();
}

core::Result<ScriptHostEventBatch>
ScriptHostEventQueue::enqueue_from_call(const ScriptModuleInfo& module, const ScriptCallDesc& call,
                                        const ScriptCallResult& result,
                                        const ScriptRuntimeDesc& runtime_desc) {
    if (call.module_id != module.module_id) {
        return core::Result<ScriptHostEventBatch>::failure(
            "scripting.host_event_module_mismatch",
            "script host event call module does not match loaded module");
    }
    if (call.stage != module.stage) {
        return core::Result<ScriptHostEventBatch>::failure(
            "scripting.host_event_stage_mismatch",
            "script host event call stage does not match loaded module stage");
    }

    auto status = validate_script_call_desc(call, runtime_desc);
    if (!status) {
        return core::Result<ScriptHostEventBatch>::failure(status.error().code,
                                                           status.error().message);
    }
    status = validate_script_call_permissions(module, call.required_permissions);
    if (!status) {
        return core::Result<ScriptHostEventBatch>::failure(status.error().code,
                                                           status.error().message);
    }
    status = validate_script_emitted_events(module, result.emitted_events, runtime_desc);
    if (!status) {
        return core::Result<ScriptHostEventBatch>::failure(status.error().code,
                                                           status.error().message);
    }

    auto batch = make_batch(next_sequence_, module, call, result);
    if (!batch) {
        return batch;
    }
    if (batch.value().events.empty()) {
        return batch;
    }

    next_sequence_ = batch.value().last_sequence + 1;
    pending_.insert(pending_.end(), batch.value().events.begin(), batch.value().events.end());
    return batch;
}

std::vector<ScriptHostEvent> ScriptHostEventQueue::drain() {
    std::vector<ScriptHostEvent> drained;
    drained.swap(pending_);
    return drained;
}

core::Status validate_script_host_event(const ScriptHostEvent& event) {
    if (event.sequence == 0) {
        return core::Status::failure("scripting.invalid_host_event_sequence",
                                     "script host event sequence must be non-zero");
    }
    if (!is_valid_script_host_api_id(event.api_id)) {
        return core::Status::failure(
            "scripting.invalid_host_api_id",
            "script host event api id must be a lowercase dotted host api id");
    }
    if (!core::PrototypeId::parse(event.module_id)) {
        return core::Status::failure("scripting.invalid_module_id",
                                     "script host event module id must be namespace:local_id");
    }
    if (!core::is_valid_namespace_id(event.source_mod_id)) {
        return core::Status::failure(
            "scripting.invalid_source_mod_id",
            "script host event source mod id must be a valid namespace id");
    }
    if (!is_valid_script_function_name(event.function_name)) {
        return core::Status::failure(
            "scripting.invalid_function_name",
            "script host event function name must contain letters, digits, underscores, or dots");
    }
    if (event.module_api_version == 0) {
        return core::Status::failure("scripting.invalid_api_version",
                                     "script host event module api version must be non-zero");
    }
    for (const auto& argument : event.arguments) {
        auto status = validate_script_value(argument, std::numeric_limits<std::uint32_t>::max());
        if (!status) {
            return status;
        }
    }
    return core::Status::ok();
}

core::Status validate_script_host_event_batch(const ScriptHostEventBatch& batch) {
    if (batch.events.empty()) {
        if (batch.first_sequence != 0 || batch.last_sequence != 0) {
            return core::Status::failure(
                "scripting.invalid_host_event_batch",
                "empty script host event batch must not declare sequence bounds");
        }
        return core::Status::ok();
    }
    if (batch.first_sequence == 0 || batch.last_sequence < batch.first_sequence) {
        return core::Status::failure(
            "scripting.invalid_host_event_batch",
            "script host event batch must declare non-zero ordered sequence bounds");
    }
    if (batch.last_sequence - batch.first_sequence + 1 != batch.events.size()) {
        return core::Status::failure(
            "scripting.invalid_host_event_batch",
            "script host event batch sequence range must match event count");
    }

    std::uint64_t expected_sequence = batch.first_sequence;
    for (const auto& event : batch.events) {
        if (event.sequence != expected_sequence++) {
            return core::Status::failure("scripting.invalid_host_event_sequence",
                                         "script host event batch sequences must be contiguous");
        }
        auto status = validate_script_host_event(event);
        if (!status) {
            return status;
        }
    }
    return core::Status::ok();
}

} // namespace heartstead::scripting
