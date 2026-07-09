#include "game/runtime/game_inspection.hpp"

#include "engine/scripting/script_runtime.hpp"

#include <sstream>
#include <utility>

namespace heartstead::game {

namespace {

void add_field(debug::InspectionData& data, std::string name, std::string value) {
    data.fields.push_back(debug::InspectionField{std::move(name), std::move(value)});
}

void add_issue(debug::InspectionData& data, debug::InspectionSeverity severity, std::string code,
               std::string message) {
    data.issues.push_back(debug::InspectionIssue{severity, std::move(code), std::move(message)});
}

void add_status_issue(debug::InspectionData& data, const core::Status& status) {
    if (!status) {
        add_issue(data, debug::InspectionSeverity::error, status.error().code,
                  status.error().message);
    }
}

[[nodiscard]] std::string argument_key_list(const std::vector<std::string>& keys) {
    std::ostringstream output;
    bool first = true;
    for (const auto& key : keys) {
        if (!first) {
            output << ',';
        }
        first = false;
        output << key;
    }
    return output.str();
}

} // namespace

debug::InspectionData GameInspector::inspect(const ScriptHostCommandRoute& route) {
    debug::InspectionData data;
    data.object_type = "script_host_command_route";
    data.display_name = route.api_id;
    data.runtime_id = route.api_id;
    data.state = std::string(scripting::script_stage_name(route.stage));
    add_field(data, "api_id", route.api_id);
    add_field(data, "command_type", route.command_type);
    add_field(data, "stage", std::string(scripting::script_stage_name(route.stage)));
    add_field(data, "required_argument_count", std::to_string(route.required_argument_keys.size()));
    add_field(data, "optional_argument_count", std::to_string(route.optional_argument_keys.size()));
    add_field(data, "required_arguments", argument_key_list(route.required_argument_keys));
    add_field(data, "optional_arguments", argument_key_list(route.optional_argument_keys));
    add_status_issue(data, validate_script_host_command_route(route));
    return data;
}

debug::InspectionData GameInspector::inspect(const ScriptHostCommandReport& report) {
    debug::InspectionData data;
    data.object_type = "script_host_command_report";
    data.display_name = report.api_id;
    data.runtime_id = std::to_string(report.command_sequence);
    data.state = report.success ? "accepted" : "rejected";
    add_field(data, "script_event_sequence", std::to_string(report.script_event_sequence));
    add_field(data, "command_sequence", std::to_string(report.command_sequence));
    add_field(data, "api_id", report.api_id);
    add_field(data, "command_type", report.command_type);
    add_field(data, "success", report.success ? "true" : "false");
    add_field(data, "committed_world_mutation", report.committed_world_mutation ? "true" : "false");
    add_field(data, "event_count", std::to_string(report.events.size()));
    add_field(data, "reserved_id_count", std::to_string(report.reserved_ids.size()));
    add_field(data, "error_code", report.error_code);
    add_field(data, "error_message", report.error_message);
    add_status_issue(data, validate_script_host_command_report(report));
    return data;
}

debug::InspectionData GameInspector::inspect(const ScriptHostCommandBatchResult& batch) {
    debug::InspectionData data;
    data.object_type = "script_host_command_batch";
    data.display_name = "Script Host Command Batch";
    data.runtime_id =
        batch.reports.empty() ? "" : std::to_string(batch.reports.front().command_sequence);
    data.state = batch.failure_count == 0 ? "accepted" : "partial_or_rejected";
    add_field(data, "command_count", std::to_string(batch.command_count));
    add_field(data, "success_count", std::to_string(batch.success_count));
    add_field(data, "failure_count", std::to_string(batch.failure_count));
    add_field(data, "report_count", std::to_string(batch.reports.size()));
    add_status_issue(data, validate_script_host_command_batch_result(batch));
    return data;
}

} // namespace heartstead::game
