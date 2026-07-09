#include "game/runtime/script_host_commands.hpp"

#include "engine/net/command_payload.hpp"

#include <cmath>
#include <iomanip>
#include <sstream>
#include <utility>

namespace heartstead::game {

namespace {

[[nodiscard]] bool has_duplicate_routes(const std::vector<ScriptHostCommandRoute>& routes) {
    for (std::size_t index = 0; index < routes.size(); ++index) {
        for (std::size_t compare_index = index + 1; compare_index < routes.size();
             ++compare_index) {
            if (routes[index].api_id == routes[compare_index].api_id) {
                return true;
            }
        }
    }
    return false;
}

[[nodiscard]] bool has_duplicate_argument_key(const ScriptHostCommandRoute& route) {
    std::vector<std::string_view> keys;
    keys.reserve(route.required_argument_keys.size() + route.optional_argument_keys.size());
    for (const auto& key : route.required_argument_keys) {
        keys.push_back(key);
    }
    for (const auto& key : route.optional_argument_keys) {
        keys.push_back(key);
    }

    for (std::size_t index = 0; index < keys.size(); ++index) {
        for (std::size_t compare_index = index + 1; compare_index < keys.size(); ++compare_index) {
            if (keys[index] == keys[compare_index]) {
                return true;
            }
        }
    }
    return false;
}

[[nodiscard]] core::Result<std::string>
script_value_to_payload_string(const scripting::ScriptValue& value) {
    switch (value.kind) {
    case scripting::ScriptValueKind::nil:
        return core::Result<std::string>::failure(
            "game_runtime.script_host_nil_argument",
            "script host command payload arguments must not be nil");
    case scripting::ScriptValueKind::boolean:
        return core::Result<std::string>::success(value.boolean_value ? "1" : "0");
    case scripting::ScriptValueKind::number: {
        if (!std::isfinite(value.number_value)) {
            return core::Result<std::string>::failure(
                "game_runtime.script_host_invalid_number",
                "script host command payload number arguments must be finite");
        }
        std::ostringstream output;
        output << std::setprecision(17) << value.number_value;
        return core::Result<std::string>::success(output.str());
    }
    case scripting::ScriptValueKind::string:
        return core::Result<std::string>::success(value.string_value);
    }
    return core::Result<std::string>::failure(
        "game_runtime.script_host_unknown_argument",
        "script host command payload argument has an unknown value kind");
}

[[nodiscard]] core::Result<std::string> make_payload(const ScriptHostCommandRoute& route,
                                                     const scripting::ScriptHostEvent& event) {
    const auto min_arguments = route.required_argument_keys.size();
    const auto max_arguments = min_arguments + route.optional_argument_keys.size();
    if (event.arguments.size() < min_arguments || event.arguments.size() > max_arguments) {
        return core::Result<std::string>::failure(
            "game_runtime.script_host_argument_count_mismatch",
            "script host event argument count does not match command route");
    }

    net::CommandPayload payload;
    std::size_t argument_index = 0;
    for (const auto& key : route.required_argument_keys) {
        auto value = script_value_to_payload_string(event.arguments[argument_index++]);
        if (!value) {
            return core::Result<std::string>::failure(value.error().code, value.error().message);
        }
        auto status = payload.set(key, std::move(value).value());
        if (!status) {
            return core::Result<std::string>::failure(status.error().code, status.error().message);
        }
    }
    for (const auto& key : route.optional_argument_keys) {
        if (argument_index >= event.arguments.size()) {
            break;
        }
        auto value = script_value_to_payload_string(event.arguments[argument_index++]);
        if (!value) {
            return core::Result<std::string>::failure(value.error().code, value.error().message);
        }
        auto status = payload.set(key, std::move(value).value());
        if (!status) {
            return core::Result<std::string>::failure(status.error().code, status.error().message);
        }
    }
    return core::Result<std::string>::success(net::CommandPayloadTextCodec::encode(payload));
}

} // namespace

ScriptHostCommandRouter::ScriptHostCommandRouter(std::vector<ScriptHostCommandRoute> routes)
    : routes_(std::move(routes)) {}

std::size_t ScriptHostCommandRouter::route_count() const noexcept {
    return routes_.size();
}

const ScriptHostCommandRoute*
ScriptHostCommandRouter::find_route(std::string_view api_id) const noexcept {
    for (const auto& route : routes_) {
        if (route.api_id == api_id) {
            return &route;
        }
    }
    return nullptr;
}

const std::vector<ScriptHostCommandRoute>& ScriptHostCommandRouter::routes() const noexcept {
    return routes_;
}

core::Result<std::vector<net::CommandEnvelope>> ScriptHostCommandRouter::make_command_envelopes(
    const std::vector<scripting::ScriptHostEvent>& events,
    ScriptHostCommandDispatchDesc desc) const {
    auto status = validate_script_host_command_routes(routes_);
    if (!status) {
        return core::Result<std::vector<net::CommandEnvelope>>::failure(status.error().code,
                                                                        status.error().message);
    }
    if (!desc.sender.is_valid()) {
        return core::Result<std::vector<net::CommandEnvelope>>::failure(
            "game_runtime.script_host_invalid_sender",
            "script host command dispatch requires a valid sender net id");
    }
    if (desc.first_sequence == 0) {
        return core::Result<std::vector<net::CommandEnvelope>>::failure(
            "game_runtime.script_host_invalid_sequence",
            "script host command dispatch first sequence must be non-zero");
    }

    std::vector<net::CommandEnvelope> envelopes;
    envelopes.reserve(events.size());
    std::uint64_t sequence = desc.first_sequence;
    for (const auto& event : events) {
        status = scripting::validate_script_host_event(event);
        if (!status) {
            return core::Result<std::vector<net::CommandEnvelope>>::failure(status.error().code,
                                                                            status.error().message);
        }
        const auto* route = find_route(event.api_id);
        if (route == nullptr) {
            return core::Result<std::vector<net::CommandEnvelope>>::failure(
                "game_runtime.script_host_route_missing",
                "script host event has no command route: " + event.api_id);
        }
        if (event.stage != route->stage) {
            return core::Result<std::vector<net::CommandEnvelope>>::failure(
                "game_runtime.script_host_route_stage_mismatch",
                "script host event stage does not match command route stage");
        }

        auto payload = make_payload(*route, event);
        if (!payload) {
            return core::Result<std::vector<net::CommandEnvelope>>::failure(
                payload.error().code, payload.error().message);
        }

        net::CommandEnvelope envelope;
        envelope.sequence = sequence++;
        envelope.sender = desc.sender;
        envelope.type = route->command_type;
        envelope.payload = std::move(payload).value();
        envelope.client_time_ms = desc.client_time_ms;
        envelopes.push_back(std::move(envelope));
    }

    return core::Result<std::vector<net::CommandEnvelope>>::success(std::move(envelopes));
}

core::Result<ScriptHostCommandBatchResult>
ScriptHostCommandRouter::dispatch_commands(const std::vector<scripting::ScriptHostEvent>& events,
                                           ScriptHostCommandDispatchDesc desc,
                                           const net::ServerCommandDispatcher& dispatcher,
                                           const net::CommandExecutionContext& context) const {
    auto envelopes = make_command_envelopes(events, desc);
    if (!envelopes) {
        return core::Result<ScriptHostCommandBatchResult>::failure(envelopes.error().code,
                                                                   envelopes.error().message);
    }

    ScriptHostCommandBatchResult batch;
    batch.command_count = static_cast<std::uint32_t>(envelopes.value().size());
    batch.reports.reserve(envelopes.value().size());

    for (std::size_t index = 0; index < envelopes.value().size(); ++index) {
        const auto& event = events[index];
        const auto& envelope = envelopes.value()[index];

        ScriptHostCommandReport report;
        report.script_event_sequence = event.sequence;
        report.command_sequence = envelope.sequence;
        report.api_id = event.api_id;
        report.command_type = envelope.type;

        auto dispatched = dispatcher.dispatch(envelope, context);
        if (dispatched) {
            report.success = true;
            report.committed_world_mutation = dispatched.value().committed_world_mutation;
            report.events = std::move(dispatched.value().events);
            report.reserved_ids = std::move(dispatched.value().reserved_ids);
            ++batch.success_count;
        } else {
            report.error_code = dispatched.error().code;
            report.error_message = dispatched.error().message;
            ++batch.failure_count;
        }

        batch.reports.push_back(std::move(report));
    }

    return core::Result<ScriptHostCommandBatchResult>::success(std::move(batch));
}

core::Status validate_script_host_command_route(const ScriptHostCommandRoute& route) {
    if (!scripting::is_valid_script_host_api_id(route.api_id)) {
        return core::Status::failure(
            "game_runtime.invalid_script_host_route_api",
            "script host command route api id must be a lowercase dotted id");
    }
    if (!scripting::is_valid_script_host_api_id(route.command_type)) {
        return core::Status::failure(
            "game_runtime.invalid_script_host_route_command",
            "script host command route command type must be a lowercase dotted id");
    }
    if (route.stage != scripting::ScriptStage::runtime_server) {
        return core::Status::failure(
            "game_runtime.invalid_script_host_route_stage",
            "script host command routes must target runtime_server host APIs");
    }
    if (has_duplicate_argument_key(route)) {
        return core::Status::failure(
            "game_runtime.duplicate_script_host_route_argument",
            "script host command route declares an argument key more than once");
    }
    for (const auto& key : route.required_argument_keys) {
        if (!net::is_valid_command_payload_key(key)) {
            return core::Status::failure("game_runtime.invalid_script_host_route_argument",
                                         "script host command route argument key is invalid: " +
                                             key);
        }
    }
    for (const auto& key : route.optional_argument_keys) {
        if (!net::is_valid_command_payload_key(key)) {
            return core::Status::failure(
                "game_runtime.invalid_script_host_route_argument",
                "script host command route optional argument key is invalid: " + key);
        }
    }
    return core::Status::ok();
}

core::Status
validate_script_host_command_routes(const std::vector<ScriptHostCommandRoute>& routes) {
    if (has_duplicate_routes(routes)) {
        return core::Status::failure("game_runtime.duplicate_script_host_route",
                                     "script host command routes contain a duplicate host api id");
    }
    for (const auto& route : routes) {
        auto status = validate_script_host_command_route(route);
        if (!status) {
            return status;
        }
    }
    return core::Status::ok();
}

core::Status validate_script_host_command_report(const ScriptHostCommandReport& report) {
    if (report.script_event_sequence == 0) {
        return core::Status::failure(
            "game_runtime.invalid_script_host_report_sequence",
            "script host command report script event sequence must be non-zero");
    }
    if (report.command_sequence == 0) {
        return core::Status::failure(
            "game_runtime.invalid_script_host_report_sequence",
            "script host command report command sequence must be non-zero");
    }
    if (!scripting::is_valid_script_host_api_id(report.api_id)) {
        return core::Status::failure(
            "game_runtime.invalid_script_host_report_api",
            "script host command report api id must be a lowercase dotted id");
    }
    if (!scripting::is_valid_script_host_api_id(report.command_type)) {
        return core::Status::failure(
            "game_runtime.invalid_script_host_report_command",
            "script host command report command type must be a lowercase dotted id");
    }
    if (report.success) {
        if (!report.error_code.empty() || !report.error_message.empty()) {
            return core::Status::failure(
                "game_runtime.invalid_script_host_report_error",
                "successful script host command report must not carry error details");
        }
    } else if (report.error_code.empty()) {
        return core::Status::failure("game_runtime.invalid_script_host_report_error",
                                     "failed script host command report must carry an error code");
    }
    return core::Status::ok();
}

core::Status validate_script_host_command_batch_result(const ScriptHostCommandBatchResult& batch) {
    if (batch.command_count != batch.reports.size()) {
        return core::Status::failure(
            "game_runtime.invalid_script_host_command_batch",
            "script host command batch command count must match report count");
    }
    if (batch.success_count + batch.failure_count != batch.command_count) {
        return core::Status::failure(
            "game_runtime.invalid_script_host_command_batch",
            "script host command batch success and failure counts must add to command count");
    }

    std::uint32_t successes = 0;
    std::uint32_t failures = 0;
    for (const auto& report : batch.reports) {
        auto status = validate_script_host_command_report(report);
        if (!status) {
            return status;
        }
        if (report.success) {
            ++successes;
        } else {
            ++failures;
        }
    }
    if (successes != batch.success_count || failures != batch.failure_count) {
        return core::Status::failure(
            "game_runtime.invalid_script_host_command_batch",
            "script host command batch counts must match report success states");
    }
    return core::Status::ok();
}

} // namespace heartstead::game
