#include "engine/replay/command_replay.hpp"

#include "engine/net/command_payload.hpp"

#include <algorithm>
#include <charconv>
#include <limits>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>

namespace heartstead::replay {

namespace {

constexpr std::string_view magic = "heartstead.command_replay.v1";

[[nodiscard]] std::string bool_text(bool value) {
    return value ? "true" : "false";
}

[[nodiscard]] bool is_hex_digit(char value) noexcept {
    return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f') ||
           (value >= 'A' && value <= 'F');
}

[[nodiscard]] char hex_value(char value) noexcept {
    if (value >= '0' && value <= '9') {
        return static_cast<char>(value - '0');
    }
    if (value >= 'a' && value <= 'f') {
        return static_cast<char>(10 + value - 'a');
    }
    return static_cast<char>(10 + value - 'A');
}

[[nodiscard]] std::string percent_escape(std::string_view input) {
    std::string result;
    result.reserve(input.size());

    constexpr char hex[] = "0123456789ABCDEF";
    for (const auto value : input) {
        const auto byte = static_cast<unsigned char>(value);
        if (value == '%' || value == '|' || value == '=' || value == '\n' || value == '\r') {
            result.push_back('%');
            result.push_back(hex[(byte >> 4u) & 0x0Fu]);
            result.push_back(hex[byte & 0x0Fu]);
        } else {
            result.push_back(value);
        }
    }

    return result;
}

[[nodiscard]] core::Result<std::string> percent_unescape(std::string_view input) {
    std::string result;
    result.reserve(input.size());

    for (std::size_t index = 0; index < input.size(); ++index) {
        if (input[index] != '%') {
            result.push_back(input[index]);
            continue;
        }

        if (index + 2 >= input.size() || !is_hex_digit(input[index + 1]) ||
            !is_hex_digit(input[index + 2])) {
            return core::Result<std::string>::failure("replay.invalid_escape",
                                                      "command replay contains an invalid escape");
        }

        const auto high = hex_value(input[index + 1]);
        const auto low = hex_value(input[index + 2]);
        result.push_back(static_cast<char>((high << 4) | low));
        index += 2;
    }

    return core::Result<std::string>::success(std::move(result));
}

[[nodiscard]] std::vector<std::string_view> split(std::string_view value, char delimiter) {
    std::vector<std::string_view> result;
    std::size_t start = 0;
    while (start <= value.size()) {
        const auto end = value.find(delimiter, start);
        if (end == std::string_view::npos) {
            result.push_back(value.substr(start));
            break;
        }
        result.push_back(value.substr(start, end - start));
        start = end + 1;
    }
    return result;
}

[[nodiscard]] core::Result<std::uint64_t> parse_u64(std::string_view value,
                                                    std::string_view field_name) {
    std::uint64_t parsed = 0;
    const auto* begin = value.data();
    const auto* end = value.data() + value.size();
    const auto [ptr, error] = std::from_chars(begin, end, parsed);
    if (error != std::errc{} || ptr != end) {
        return core::Result<std::uint64_t>::failure(
            "replay.invalid_number", "invalid replay numeric field: " + std::string(field_name));
    }
    return core::Result<std::uint64_t>::success(parsed);
}

[[nodiscard]] core::Result<std::int64_t> parse_i64(std::string_view value,
                                                   std::string_view field_name) {
    std::int64_t parsed = 0;
    const auto* begin = value.data();
    const auto* end = value.data() + value.size();
    const auto [ptr, error] = std::from_chars(begin, end, parsed);
    if (error != std::errc{} || ptr != end) {
        return core::Result<std::int64_t>::failure(
            "replay.invalid_number", "invalid replay numeric field: " + std::string(field_name));
    }
    return core::Result<std::int64_t>::success(parsed);
}

[[nodiscard]] core::Result<std::uint32_t> parse_u32(std::string_view value,
                                                    std::string_view field_name) {
    auto parsed = parse_u64(value, field_name);
    if (!parsed) {
        return core::Result<std::uint32_t>::failure(parsed.error().code, parsed.error().message);
    }
    if (parsed.value() > std::numeric_limits<std::uint32_t>::max()) {
        return core::Result<std::uint32_t>::failure("replay.number_out_of_range",
                                                    "replay numeric field is too large: " +
                                                        std::string(field_name));
    }
    return core::Result<std::uint32_t>::success(static_cast<std::uint32_t>(parsed.value()));
}

[[nodiscard]] core::Result<std::size_t> parse_size(std::string_view value,
                                                   std::string_view field_name) {
    auto parsed = parse_u64(value, field_name);
    if (!parsed) {
        return core::Result<std::size_t>::failure(parsed.error().code, parsed.error().message);
    }
    if (parsed.value() > std::numeric_limits<std::size_t>::max()) {
        return core::Result<std::size_t>::failure("replay.number_out_of_range",
                                                  "replay numeric field is too large: " +
                                                      std::string(field_name));
    }
    return core::Result<std::size_t>::success(static_cast<std::size_t>(parsed.value()));
}

[[nodiscard]] core::Result<bool> parse_bool(std::string_view value, std::string_view field_name) {
    if (value == "true") {
        return core::Result<bool>::success(true);
    }
    if (value == "false") {
        return core::Result<bool>::success(false);
    }
    return core::Result<bool>::failure("replay.invalid_bool",
                                       "invalid replay boolean field: " + std::string(field_name));
}

[[nodiscard]] core::Result<core::SaveId> parse_save_id(std::string_view value,
                                                       std::string_view field_name) {
    auto parsed = parse_u64(value, field_name);
    if (!parsed) {
        return core::Result<core::SaveId>::failure(parsed.error().code, parsed.error().message);
    }
    auto id = core::SaveId::from_value(parsed.value());
    if (!id.is_valid()) {
        return core::Result<core::SaveId>::failure("replay.invalid_save_id",
                                                   "expected replay save id must be non-zero");
    }
    return core::Result<core::SaveId>::success(id);
}

[[nodiscard]] core::Result<world::OperationStage> parse_stage(std::string_view value) {
    if (value == "begun") {
        return core::Result<world::OperationStage>::success(world::OperationStage::begun);
    }
    if (value == "validated") {
        return core::Result<world::OperationStage>::success(world::OperationStage::validated);
    }
    if (value == "ids_reserved") {
        return core::Result<world::OperationStage>::success(world::OperationStage::ids_reserved);
    }
    if (value == "mutated") {
        return core::Result<world::OperationStage>::success(world::OperationStage::mutated);
    }
    if (value == "derived_updated") {
        return core::Result<world::OperationStage>::success(world::OperationStage::derived_updated);
    }
    if (value == "events_emitted") {
        return core::Result<world::OperationStage>::success(world::OperationStage::events_emitted);
    }
    if (value == "replication_marked") {
        return core::Result<world::OperationStage>::success(
            world::OperationStage::replication_marked);
    }
    if (value == "save_marked") {
        return core::Result<world::OperationStage>::success(world::OperationStage::save_marked);
    }
    if (value == "committed") {
        return core::Result<world::OperationStage>::success(world::OperationStage::committed);
    }
    if (value == "rolled_back") {
        return core::Result<world::OperationStage>::success(world::OperationStage::rolled_back);
    }
    return core::Result<world::OperationStage>::failure(
        "replay.invalid_stage", "unknown replay operation stage: " + std::string(value));
}

void set_payload_field(net::CommandPayload& payload, std::string key, std::string value) {
    (void)payload.set(std::move(key), std::move(value));
}

[[nodiscard]] core::Result<std::vector<std::string>>
parse_indexed_strings(const net::CommandPayload& payload, std::string_view prefix) {
    std::vector<std::pair<std::size_t, std::string>> entries;
    const auto prefix_text = std::string(prefix) + ".";
    for (const auto& [key, value] : payload.fields()) {
        if (!key.starts_with(prefix_text)) {
            continue;
        }
        auto index = parse_size(std::string_view(key).substr(prefix_text.size()), prefix);
        if (!index) {
            return core::Result<std::vector<std::string>>::failure(index.error().code,
                                                                   index.error().message);
        }
        entries.emplace_back(index.value(), value);
    }

    std::ranges::sort(entries, {}, &std::pair<std::size_t, std::string>::first);
    std::vector<std::string> result;
    result.reserve(entries.size());
    for (std::size_t index = 0; index < entries.size(); ++index) {
        if (entries[index].first != index) {
            return core::Result<std::vector<std::string>>::failure(
                "replay.non_contiguous_expectation",
                "indexed replay expectation fields must start at zero and be contiguous: " +
                    std::string(prefix));
        }
        result.push_back(std::move(entries[index].second));
    }
    return core::Result<std::vector<std::string>>::success(std::move(result));
}

[[nodiscard]] core::Result<std::vector<core::SaveId>>
parse_indexed_save_ids(const net::CommandPayload& payload, std::string_view prefix) {
    std::vector<std::pair<std::size_t, core::SaveId>> entries;
    const auto prefix_text = std::string(prefix) + ".";
    for (const auto& [key, value] : payload.fields()) {
        if (!key.starts_with(prefix_text)) {
            continue;
        }
        auto index = parse_size(std::string_view(key).substr(prefix_text.size()), prefix);
        if (!index) {
            return core::Result<std::vector<core::SaveId>>::failure(index.error().code,
                                                                    index.error().message);
        }
        auto id = parse_save_id(value, key);
        if (!id) {
            return core::Result<std::vector<core::SaveId>>::failure(id.error().code,
                                                                    id.error().message);
        }
        entries.emplace_back(index.value(), id.value());
    }

    std::ranges::sort(entries, {}, &std::pair<std::size_t, core::SaveId>::first);
    std::vector<core::SaveId> result;
    result.reserve(entries.size());
    for (std::size_t index = 0; index < entries.size(); ++index) {
        if (entries[index].first != index) {
            return core::Result<std::vector<core::SaveId>>::failure(
                "replay.non_contiguous_expectation",
                "indexed replay expectation fields must start at zero and be contiguous: " +
                    std::string(prefix));
        }
        result.push_back(entries[index].second);
    }
    return core::Result<std::vector<core::SaveId>>::success(std::move(result));
}

[[nodiscard]] core::Result<CommandReplayExpectation> parse_expectation(std::string_view value) {
    auto payload = net::CommandPayloadTextCodec::decode(value);
    if (!payload) {
        return core::Result<CommandReplayExpectation>::failure(payload.error().code,
                                                               payload.error().message);
    }

    CommandReplayExpectation expectation;
    if (const auto* committed = payload.value().find("committed_world_mutation")) {
        auto parsed = parse_bool(*committed, "committed_world_mutation");
        if (!parsed) {
            return core::Result<CommandReplayExpectation>::failure(parsed.error().code,
                                                                   parsed.error().message);
        }
        expectation.has_committed_world_mutation = true;
        expectation.committed_world_mutation = parsed.value();
    }
    if (const auto* count = payload.value().find("event_count")) {
        auto parsed = parse_size(*count, "event_count");
        if (!parsed) {
            return core::Result<CommandReplayExpectation>::failure(parsed.error().code,
                                                                   parsed.error().message);
        }
        expectation.event_count = parsed.value();
    }
    if (const auto* count = payload.value().find("reserved_id_count")) {
        auto parsed = parse_size(*count, "reserved_id_count");
        if (!parsed) {
            return core::Result<CommandReplayExpectation>::failure(parsed.error().code,
                                                                   parsed.error().message);
        }
        expectation.reserved_id_count = parsed.value();
    }
    if (const auto* dirty = payload.value().find("replication_dirty")) {
        auto parsed = parse_bool(*dirty, "replication_dirty");
        if (!parsed) {
            return core::Result<CommandReplayExpectation>::failure(parsed.error().code,
                                                                   parsed.error().message);
        }
        expectation.replication_dirty = parsed.value();
    }
    if (const auto* dirty = payload.value().find("save_dirty")) {
        auto parsed = parse_bool(*dirty, "save_dirty");
        if (!parsed) {
            return core::Result<CommandReplayExpectation>::failure(parsed.error().code,
                                                                   parsed.error().message);
        }
        expectation.save_dirty = parsed.value();
    }
    if (const auto* stage = payload.value().find("last_stage")) {
        auto parsed = parse_stage(*stage);
        if (!parsed) {
            return core::Result<CommandReplayExpectation>::failure(parsed.error().code,
                                                                   parsed.error().message);
        }
        expectation.last_stage = parsed.value();
    }
    if (const auto* error_code = payload.value().find("error_code")) {
        expectation.error_code = *error_code;
    }
    if (const auto* error_message = payload.value().find("error_message")) {
        expectation.error_message = *error_message;
    }

    auto event_types = parse_indexed_strings(payload.value(), "event_type");
    if (!event_types) {
        return core::Result<CommandReplayExpectation>::failure(event_types.error().code,
                                                               event_types.error().message);
    }
    expectation.event_types = std::move(event_types).value();

    auto reserved_ids = parse_indexed_save_ids(payload.value(), "reserved_id");
    if (!reserved_ids) {
        return core::Result<CommandReplayExpectation>::failure(reserved_ids.error().code,
                                                               reserved_ids.error().message);
    }
    expectation.reserved_ids = std::move(reserved_ids).value();

    auto mutations = parse_indexed_strings(payload.value(), "mutation");
    if (!mutations) {
        return core::Result<CommandReplayExpectation>::failure(mutations.error().code,
                                                               mutations.error().message);
    }
    expectation.mutations = std::move(mutations).value();

    auto derived_updates = parse_indexed_strings(payload.value(), "derived_update");
    if (!derived_updates) {
        return core::Result<CommandReplayExpectation>::failure(derived_updates.error().code,
                                                               derived_updates.error().message);
    }
    expectation.derived_updates = std::move(derived_updates).value();

    auto status = expectation.validate();
    if (!status) {
        return core::Result<CommandReplayExpectation>::failure(status.error().code,
                                                               status.error().message);
    }
    return core::Result<CommandReplayExpectation>::success(std::move(expectation));
}

[[nodiscard]] std::string encode_expectation(const CommandReplayExpectation& expectation) {
    net::CommandPayload payload;
    if (expectation.has_committed_world_mutation) {
        set_payload_field(payload, "committed_world_mutation",
                          bool_text(expectation.committed_world_mutation));
    }
    if (expectation.event_count.has_value()) {
        set_payload_field(payload, "event_count", std::to_string(*expectation.event_count));
    }
    if (expectation.reserved_id_count.has_value()) {
        set_payload_field(payload, "reserved_id_count",
                          std::to_string(*expectation.reserved_id_count));
    }
    if (expectation.replication_dirty.has_value()) {
        set_payload_field(payload, "replication_dirty", bool_text(*expectation.replication_dirty));
    }
    if (expectation.save_dirty.has_value()) {
        set_payload_field(payload, "save_dirty", bool_text(*expectation.save_dirty));
    }
    if (expectation.last_stage.has_value()) {
        set_payload_field(payload, "last_stage",
                          std::string(world::operation_stage_name(*expectation.last_stage)));
    }
    if (expectation.error_code.has_value()) {
        set_payload_field(payload, "error_code", *expectation.error_code);
    }
    if (expectation.error_message.has_value()) {
        set_payload_field(payload, "error_message", *expectation.error_message);
    }
    for (std::size_t index = 0; index < expectation.event_types.size(); ++index) {
        set_payload_field(payload, "event_type." + std::to_string(index),
                          expectation.event_types[index]);
    }
    for (std::size_t index = 0; index < expectation.reserved_ids.size(); ++index) {
        set_payload_field(payload, "reserved_id." + std::to_string(index),
                          std::to_string(expectation.reserved_ids[index].value()));
    }
    for (std::size_t index = 0; index < expectation.mutations.size(); ++index) {
        set_payload_field(payload, "mutation." + std::to_string(index),
                          expectation.mutations[index]);
    }
    for (std::size_t index = 0; index < expectation.derived_updates.size(); ++index) {
        set_payload_field(payload, "derived_update." + std::to_string(index),
                          expectation.derived_updates[index]);
    }
    return net::CommandPayloadTextCodec::encode(payload);
}

[[nodiscard]] core::Result<RecordedCommand> parse_recorded_command(std::string_view value) {
    const auto parts = split(value, '|');
    if (parts.size() != 6) {
        return core::Result<RecordedCommand>::failure(
            "replay.invalid_command_record", "command record must contain sequence, sender, client "
                                             "time, server time, type, payload");
    }

    auto sequence = parse_u64(parts[0], "sequence");
    auto sender = parse_u64(parts[1], "sender");
    auto client_time = parse_i64(parts[2], "client_time_ms");
    auto server_time = parse_i64(parts[3], "server_time_ms");
    auto type = percent_unescape(parts[4]);
    auto payload = percent_unescape(parts[5]);
    if (!sequence) {
        return core::Result<RecordedCommand>::failure(sequence.error().code,
                                                      sequence.error().message);
    }
    if (!sender) {
        return core::Result<RecordedCommand>::failure(sender.error().code, sender.error().message);
    }
    if (!client_time) {
        return core::Result<RecordedCommand>::failure(client_time.error().code,
                                                      client_time.error().message);
    }
    if (!server_time) {
        return core::Result<RecordedCommand>::failure(server_time.error().code,
                                                      server_time.error().message);
    }
    if (!type) {
        return core::Result<RecordedCommand>::failure(type.error().code, type.error().message);
    }
    if (!payload) {
        return core::Result<RecordedCommand>::failure(payload.error().code,
                                                      payload.error().message);
    }

    RecordedCommand command;
    command.envelope.sequence = sequence.value();
    command.envelope.sender = core::NetId::from_value(sender.value());
    command.envelope.client_time_ms = client_time.value();
    command.envelope.type = std::move(type).value();
    command.envelope.payload = std::move(payload).value();
    command.server_time_ms = server_time.value();
    return core::Result<RecordedCommand>::success(std::move(command));
}

[[nodiscard]] core::Status expectation_mismatch(std::string field, std::string expected,
                                                std::string actual) {
    return core::Status::failure("replay.expectation_mismatch", "replay expectation mismatch for " +
                                                                    field + ": expected " +
                                                                    expected + ", got " + actual);
}

[[nodiscard]] core::Status compare_string_vector(std::string_view field,
                                                 const std::vector<std::string>& expected,
                                                 const std::vector<std::string>& actual) {
    if (expected.size() != actual.size()) {
        return expectation_mismatch(std::string(field) + ".size", std::to_string(expected.size()),
                                    std::to_string(actual.size()));
    }
    for (std::size_t index = 0; index < expected.size(); ++index) {
        if (expected[index] != actual[index]) {
            return expectation_mismatch(std::string(field) + "." + std::to_string(index),
                                        expected[index], actual[index]);
        }
    }
    return core::Status::ok();
}

[[nodiscard]] core::Status compare_save_id_vector(std::string_view field,
                                                  const std::vector<core::SaveId>& expected,
                                                  const std::vector<core::SaveId>& actual) {
    if (expected.size() != actual.size()) {
        return expectation_mismatch(std::string(field) + ".size", std::to_string(expected.size()),
                                    std::to_string(actual.size()));
    }
    for (std::size_t index = 0; index < expected.size(); ++index) {
        if (expected[index] != actual[index]) {
            return expectation_mismatch(std::string(field) + "." + std::to_string(index),
                                        expected[index].to_string(), actual[index].to_string());
        }
    }
    return core::Status::ok();
}

[[nodiscard]] std::vector<std::string>
event_types_from_events(const std::vector<world::OperationEvent>& events) {
    std::vector<std::string> types;
    types.reserve(events.size());
    for (const auto& event : events) {
        types.push_back(event.type);
    }
    return types;
}

[[nodiscard]] core::Status verify_expectation(const CommandReplayExpectation& expectation,
                                              const CommandReplayStep& step) {
    if (expectation.has_committed_world_mutation &&
        expectation.committed_world_mutation != step.committed_world_mutation) {
        return expectation_mismatch("committed_world_mutation",
                                    bool_text(expectation.committed_world_mutation),
                                    bool_text(step.committed_world_mutation));
    }
    if (expectation.event_count.has_value() && *expectation.event_count != step.events.size()) {
        return expectation_mismatch("event_count", std::to_string(*expectation.event_count),
                                    std::to_string(step.events.size()));
    }
    if (expectation.reserved_id_count.has_value() &&
        *expectation.reserved_id_count != step.reserved_ids.size()) {
        return expectation_mismatch("reserved_id_count",
                                    std::to_string(*expectation.reserved_id_count),
                                    std::to_string(step.reserved_ids.size()));
    }
    if (!expectation.event_types.empty()) {
        auto status = compare_string_vector("event_type", expectation.event_types,
                                            event_types_from_events(step.events));
        if (!status) {
            return status;
        }
    }
    if (!expectation.reserved_ids.empty()) {
        auto status =
            compare_save_id_vector("reserved_id", expectation.reserved_ids, step.reserved_ids);
        if (!status) {
            return status;
        }
    }
    if (!expectation.mutations.empty()) {
        auto status = compare_string_vector("mutation", expectation.mutations,
                                            step.operation_trace.mutations);
        if (!status) {
            return status;
        }
    }
    if (!expectation.derived_updates.empty()) {
        auto status = compare_string_vector("derived_update", expectation.derived_updates,
                                            step.operation_trace.derived_updates);
        if (!status) {
            return status;
        }
    }
    if (expectation.replication_dirty.has_value() &&
        *expectation.replication_dirty != step.operation_trace.replication_dirty) {
        return expectation_mismatch("replication_dirty", bool_text(*expectation.replication_dirty),
                                    bool_text(step.operation_trace.replication_dirty));
    }
    if (expectation.save_dirty.has_value() &&
        *expectation.save_dirty != step.operation_trace.save_dirty) {
        return expectation_mismatch("save_dirty", bool_text(*expectation.save_dirty),
                                    bool_text(step.operation_trace.save_dirty));
    }
    if (expectation.last_stage.has_value()) {
        if (step.operation_trace.stages.empty()) {
            return expectation_mismatch(
                "last_stage", std::string(world::operation_stage_name(*expectation.last_stage)),
                "none");
        }
        const auto actual = step.operation_trace.stages.back();
        if (*expectation.last_stage != actual) {
            return expectation_mismatch(
                "last_stage", std::string(world::operation_stage_name(*expectation.last_stage)),
                std::string(world::operation_stage_name(actual)));
        }
    }
    if (expectation.error_code.has_value() && *expectation.error_code != step.error_code) {
        return expectation_mismatch("error_code", *expectation.error_code, step.error_code);
    }
    if (expectation.error_message.has_value() && *expectation.error_message != step.error_message) {
        return expectation_mismatch("error_message", *expectation.error_message,
                                    step.error_message);
    }
    return core::Status::ok();
}

} // namespace

bool CommandReplayExpectation::empty() const noexcept {
    return !has_committed_world_mutation && !event_count.has_value() &&
           !reserved_id_count.has_value() && event_types.empty() && reserved_ids.empty() &&
           mutations.empty() && derived_updates.empty() && !replication_dirty.has_value() &&
           !save_dirty.has_value() && !last_stage.has_value() && !error_code.has_value() &&
           !error_message.has_value();
}

core::Status CommandReplayExpectation::validate() const {
    if (event_count.has_value() && !event_types.empty() && *event_count != event_types.size()) {
        return core::Status::failure(
            "replay.inconsistent_expectation",
            "event_count must match indexed event_type fields when both are present");
    }
    if (reserved_id_count.has_value() && !reserved_ids.empty() &&
        *reserved_id_count != reserved_ids.size()) {
        return core::Status::failure(
            "replay.inconsistent_expectation",
            "reserved_id_count must match indexed reserved_id fields when both are present");
    }
    if (error_message.has_value() && !error_code.has_value()) {
        return core::Status::failure("replay.inconsistent_expectation",
                                     "error_message expectation requires error_code expectation");
    }
    for (const auto& id : reserved_ids) {
        if (!id.is_valid()) {
            return core::Status::failure("replay.invalid_save_id",
                                         "expected replay save ids must be valid");
        }
    }
    return core::Status::ok();
}

core::Status CommandReplayLog::validate() const {
    if (version == 0) {
        return core::Status::failure("replay.invalid_version", "replay version must be non-zero");
    }
    if (scenario_id.empty()) {
        return core::Status::failure("replay.missing_scenario", "replay scenario id is required");
    }

    std::uint64_t previous_sequence = 0;
    bool have_previous_sequence = false;
    for (const auto& command : commands) {
        if (command.envelope.sequence == 0) {
            return core::Status::failure("replay.invalid_sequence",
                                         "recorded commands need non-zero sequence ids");
        }
        if (have_previous_sequence && command.envelope.sequence <= previous_sequence) {
            return core::Status::failure(
                "replay.sequence_not_increasing",
                "recorded command sequence ids must be strictly increasing");
        }
        if (!command.envelope.sender.is_valid()) {
            return core::Status::failure("replay.invalid_sender",
                                         "recorded command sender net id must be valid");
        }
        if (command.envelope.type.empty()) {
            return core::Status::failure("replay.missing_command_type",
                                         "recorded command type is required");
        }
        if (command.server_time_ms < command.envelope.client_time_ms) {
            return core::Status::failure("replay.time_order",
                                         "recorded server time must not precede client time");
        }
        auto expectation_status = command.expectation.validate();
        if (!expectation_status) {
            return expectation_status;
        }

        previous_sequence = command.envelope.sequence;
        have_previous_sequence = true;
    }

    return core::Status::ok();
}

std::string CommandReplayCodec::encode(const CommandReplayLog& log) {
    std::ostringstream output;
    output << magic << '\n';
    output << "version=" << log.version << '\n';
    output << "scenario=" << percent_escape(log.scenario_id) << '\n';
    output << "world_seed=" << log.world_seed << '\n';

    for (const auto& command : log.commands) {
        output << "command=" << command.envelope.sequence << '|' << command.envelope.sender.value()
               << '|' << command.envelope.client_time_ms << '|' << command.server_time_ms << '|'
               << percent_escape(command.envelope.type) << '|'
               << percent_escape(command.envelope.payload) << '\n';
        if (!command.expectation.empty()) {
            output << "expect=" << encode_expectation(command.expectation) << '\n';
        }
    }

    output << "end\n";
    return output.str();
}

core::Result<CommandReplayLog> CommandReplayCodec::decode(std::string_view text) {
    CommandReplayLog log;
    bool saw_magic = false;
    bool saw_end = false;
    bool saw_version = false;
    bool saw_scenario = false;
    bool saw_world_seed = false;

    std::size_t line_start = 0;
    while (line_start <= text.size()) {
        const auto line_end = text.find('\n', line_start);
        auto line = line_end == std::string_view::npos
                        ? text.substr(line_start)
                        : text.substr(line_start, line_end - line_start);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }

        if (!saw_magic) {
            if (line != magic) {
                return core::Result<CommandReplayLog>::failure(
                    "replay.invalid_magic", "replay does not start with expected magic");
            }
            saw_magic = true;
        } else if (line == "end") {
            saw_end = true;
            break;
        } else if (!line.empty()) {
            const auto separator = line.find('=');
            if (separator == std::string_view::npos) {
                return core::Result<CommandReplayLog>::failure(
                    "replay.invalid_line", "replay line is missing key/value separator");
            }

            const auto key = line.substr(0, separator);
            const auto value = line.substr(separator + 1);

            if (key == "version") {
                auto parsed = parse_u32(value, key);
                if (!parsed) {
                    return core::Result<CommandReplayLog>::failure(parsed.error().code,
                                                                   parsed.error().message);
                }
                log.version = parsed.value();
                saw_version = true;
            } else if (key == "scenario") {
                auto parsed = percent_unescape(value);
                if (!parsed) {
                    return core::Result<CommandReplayLog>::failure(parsed.error().code,
                                                                   parsed.error().message);
                }
                log.scenario_id = std::move(parsed).value();
                saw_scenario = true;
            } else if (key == "world_seed") {
                auto parsed = parse_u64(value, key);
                if (!parsed) {
                    return core::Result<CommandReplayLog>::failure(parsed.error().code,
                                                                   parsed.error().message);
                }
                log.world_seed = parsed.value();
                saw_world_seed = true;
            } else if (key == "command") {
                auto parsed = parse_recorded_command(value);
                if (!parsed) {
                    return core::Result<CommandReplayLog>::failure(parsed.error().code,
                                                                   parsed.error().message);
                }
                log.commands.push_back(std::move(parsed).value());
            } else if (key == "expect") {
                if (log.commands.empty()) {
                    return core::Result<CommandReplayLog>::failure(
                        "replay.expectation_without_command",
                        "replay expectation must follow a command record");
                }
                if (!log.commands.back().expectation.empty()) {
                    return core::Result<CommandReplayLog>::failure(
                        "replay.duplicate_expectation",
                        "command record has more than one replay expectation");
                }
                auto parsed = parse_expectation(value);
                if (!parsed) {
                    return core::Result<CommandReplayLog>::failure(parsed.error().code,
                                                                   parsed.error().message);
                }
                log.commands.back().expectation = std::move(parsed).value();
            } else {
                return core::Result<CommandReplayLog>::failure(
                    "replay.unknown_key", "unknown replay key: " + std::string(key));
            }
        }

        if (line_end == std::string_view::npos) {
            break;
        }
        line_start = line_end + 1;
    }

    if (!saw_magic || !saw_end || !saw_version || !saw_scenario || !saw_world_seed) {
        return core::Result<CommandReplayLog>::failure("replay.incomplete",
                                                       "replay is missing required fields");
    }

    auto status = log.validate();
    if (!status) {
        return core::Result<CommandReplayLog>::failure(status.error().code, status.error().message);
    }

    return core::Result<CommandReplayLog>::success(std::move(log));
}

core::Result<CommandReplayReport>
CommandReplayRunner::run(const CommandReplayLog& log,
                         const net::ServerCommandDispatcher& dispatcher,
                         net::CommandExecutionContext context) {
    auto validation = log.validate();
    if (!validation) {
        return core::Result<CommandReplayReport>::failure(validation.error().code,
                                                          validation.error().message);
    }

    CommandReplayReport report;
    context.executor_role = net::CommandExecutorRole::authoritative_server;

    for (std::size_t index = 0; index < log.commands.size(); ++index) {
        const auto& command = log.commands[index];
        context.server_time_ms = command.server_time_ms;

        auto dispatch = dispatcher.dispatch_report(command.envelope, context);

        CommandReplayStep step;
        step.index = index;
        step.sequence = dispatch.sequence;
        step.command_type = dispatch.command_type;
        step.success = dispatch.succeeded;
        step.committed_world_mutation = dispatch.committed_world_mutation;
        step.events = dispatch.events;
        step.reserved_ids = dispatch.reserved_ids;
        step.operation_trace = dispatch.operation_trace;
        if (dispatch.error.has_value()) {
            step.error_code = dispatch.error->code;
            step.error_message = dispatch.error->message;
        }
        if (!command.expectation.empty()) {
            auto expectation_status = verify_expectation(command.expectation, step);
            if (!expectation_status) {
                return core::Result<CommandReplayReport>::failure(
                    expectation_status.error().code, expectation_status.error().message);
            }
            step.expectation_checked = true;
        }
        report.steps.push_back(std::move(step));
    }

    return core::Result<CommandReplayReport>::success(std::move(report));
}

} // namespace heartstead::replay
