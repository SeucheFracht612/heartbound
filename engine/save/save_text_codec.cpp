#include "engine/save/save_text_codec.hpp"

#include "engine/math/vector.hpp"

#include <charconv>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace heartstead::save {

namespace {

constexpr std::string_view magic = "heartstead.save_text.v1";
constexpr std::string_view snapshot_magic_v1 = "heartstead.save_snapshot_text.v1";
constexpr std::string_view snapshot_magic_v2 = "heartstead.save_snapshot_text.v2";

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
        if (value == '%' || value == '|' || value == '=' || value == ',' || value == '~' ||
            value == '\n' || value == '\r') {
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
            return core::Result<std::string>::failure("save_text.invalid_escape",
                                                      "save text contains an invalid escape");
        }

        const auto high = hex_value(input[index + 1]);
        const auto low = hex_value(input[index + 2]);
        result.push_back(static_cast<char>((high << 4) | low));
        index += 2;
    }

    return core::Result<std::string>::success(std::move(result));
}

[[nodiscard]] core::Result<std::uint64_t> parse_u64(std::string_view value,
                                                    std::string_view field_name) {
    std::uint64_t parsed = 0;
    const auto* begin = value.data();
    const auto* end = value.data() + value.size();
    const auto [ptr, error] = std::from_chars(begin, end, parsed);
    if (error != std::errc{} || ptr != end) {
        return core::Result<std::uint64_t>::failure("save_text.invalid_number",
                                                    "invalid numeric save metadata field: " +
                                                        std::string(field_name));
    }
    return core::Result<std::uint64_t>::success(parsed);
}

[[nodiscard]] core::Result<std::uint32_t> parse_u32(std::string_view value,
                                                    std::string_view field_name) {
    auto parsed = parse_u64(value, field_name);
    if (!parsed) {
        return core::Result<std::uint32_t>::failure(parsed.error().code, parsed.error().message);
    }
    if (parsed.value() > std::numeric_limits<std::uint32_t>::max()) {
        return core::Result<std::uint32_t>::failure("save_text.number_out_of_range",
                                                    "numeric save metadata field is too large: " +
                                                        std::string(field_name));
    }
    return core::Result<std::uint32_t>::success(static_cast<std::uint32_t>(parsed.value()));
}

[[nodiscard]] core::Result<std::uint16_t> parse_u16(std::string_view value,
                                                    std::string_view field_name) {
    auto parsed = parse_u64(value, field_name);
    if (!parsed) {
        return core::Result<std::uint16_t>::failure(parsed.error().code, parsed.error().message);
    }
    if (parsed.value() > std::numeric_limits<std::uint16_t>::max()) {
        return core::Result<std::uint16_t>::failure("save_text.number_out_of_range",
                                                    "numeric save field is too large: " +
                                                        std::string(field_name));
    }
    return core::Result<std::uint16_t>::success(static_cast<std::uint16_t>(parsed.value()));
}

[[nodiscard]] core::Result<std::int64_t> parse_i64(std::string_view value,
                                                   std::string_view field_name) {
    std::int64_t parsed = 0;
    const auto* begin = value.data();
    const auto* end = value.data() + value.size();
    const auto [ptr, error] = std::from_chars(begin, end, parsed);
    if (error != std::errc{} || ptr != end) {
        return core::Result<std::int64_t>::failure("save_text.invalid_number",
                                                   "invalid signed numeric save field: " +
                                                       std::string(field_name));
    }
    return core::Result<std::int64_t>::success(parsed);
}

[[nodiscard]] core::Result<std::int32_t> parse_i32(std::string_view value,
                                                   std::string_view field_name) {
    auto parsed = parse_i64(value, field_name);
    if (!parsed) {
        return core::Result<std::int32_t>::failure(parsed.error().code, parsed.error().message);
    }
    if (parsed.value() < std::numeric_limits<std::int32_t>::min() ||
        parsed.value() > std::numeric_limits<std::int32_t>::max()) {
        return core::Result<std::int32_t>::failure("save_text.number_out_of_range",
                                                   "numeric save field is out of range: " +
                                                       std::string(field_name));
    }
    return core::Result<std::int32_t>::success(static_cast<std::int32_t>(parsed.value()));
}

[[nodiscard]] core::Result<double> parse_double(std::string_view value,
                                                std::string_view field_name) {
    double parsed = 0.0;
    std::istringstream input{std::string(value)};
    input >> parsed;
    if (!input || !input.eof()) {
        return core::Result<double>::failure("save_text.invalid_number",
                                             "invalid floating-point save field: " +
                                                 std::string(field_name));
    }
    return core::Result<double>::success(parsed);
}

[[nodiscard]] core::Result<bool> parse_bool(std::string_view value, std::string_view field_name) {
    if (value == "0") {
        return core::Result<bool>::success(false);
    }
    if (value == "1") {
        return core::Result<bool>::success(true);
    }
    return core::Result<bool>::failure(
        "save_text.invalid_bool", "boolean save field must be 0 or 1: " + std::string(field_name));
}

[[nodiscard]] std::string encode_double(double value) {
    std::ostringstream output;
    output << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
    return output.str();
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

[[nodiscard]] core::Result<SavedModRecord> parse_mod_record(std::string_view value) {
    const auto parts = split(value, '|');
    if (parts.size() != 3) {
        return core::Result<SavedModRecord>::failure(
            "save_text.invalid_mod_record",
            "mod record must contain id, version, and prototype hash");
    }

    auto id = percent_unescape(parts[0]);
    auto version = percent_unescape(parts[1]);
    auto prototype_hash = percent_unescape(parts[2]);
    if (!id || !version || !prototype_hash) {
        return core::Result<SavedModRecord>::failure("save_text.invalid_mod_record",
                                                     "mod record contains invalid escaping");
    }

    return core::Result<SavedModRecord>::success(SavedModRecord{
        std::move(id).value(), std::move(version).value(), std::move(prototype_hash).value()});
}

[[nodiscard]] core::Result<core::PrototypeId> parse_prototype_id(std::string_view value) {
    auto unescaped = percent_unescape(value);
    if (!unescaped) {
        return core::Result<core::PrototypeId>::failure(unescaped.error().code,
                                                        unescaped.error().message);
    }
    auto parsed = core::PrototypeId::parse(unescaped.value());
    if (!parsed) {
        return core::Result<core::PrototypeId>::failure("save_text.invalid_prototype",
                                                        "saved prototype id is invalid");
    }
    return core::Result<core::PrototypeId>::success(parsed.value());
}

[[nodiscard]] std::string encode_string_list(const std::vector<std::string>& values) {
    std::ostringstream output;
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0) {
            output << ',';
        }
        output << percent_escape(values[index]);
    }
    return output.str();
}

[[nodiscard]] core::Result<std::vector<std::string>> parse_string_list(std::string_view value) {
    std::vector<std::string> result;
    if (value.empty()) {
        return core::Result<std::vector<std::string>>::success(std::move(result));
    }

    for (const auto part : split(value, ',')) {
        auto parsed = percent_unescape(part);
        if (!parsed) {
            return core::Result<std::vector<std::string>>::failure(parsed.error().code,
                                                                   parsed.error().message);
        }
        result.push_back(std::move(parsed).value());
    }

    return core::Result<std::vector<std::string>>::success(std::move(result));
}

[[nodiscard]] std::string construction_state_name(build::ConstructionState state) {
    switch (state) {
    case build::ConstructionState::planned:
        return "planned";
    case build::ConstructionState::under_construction:
        return "under_construction";
    case build::ConstructionState::complete:
        return "complete";
    case build::ConstructionState::damaged:
        return "damaged";
    }
    return "unknown";
}

[[nodiscard]] core::Result<build::ConstructionState>
parse_construction_state(std::string_view value) {
    if (value == "planned") {
        return core::Result<build::ConstructionState>::success(build::ConstructionState::planned);
    }
    if (value == "under_construction") {
        return core::Result<build::ConstructionState>::success(
            build::ConstructionState::under_construction);
    }
    if (value == "complete") {
        return core::Result<build::ConstructionState>::success(build::ConstructionState::complete);
    }
    if (value == "damaged") {
        return core::Result<build::ConstructionState>::success(build::ConstructionState::damaged);
    }
    return core::Result<build::ConstructionState>::failure("save_text.invalid_construction_state",
                                                           "unknown construction state: " +
                                                               std::string(value));
}

[[nodiscard]] std::string save_network_kind_name(networks::NetworkKind kind) {
    switch (kind) {
    case networks::NetworkKind::road:
        return "road";
    case networks::NetworkKind::cart_access:
        return "cart_access";
    case networks::NetworkKind::storage_access:
        return "storage_access";
    case networks::NetworkKind::power:
        return "power";
    case networks::NetworkKind::ward:
        return "ward";
    case networks::NetworkKind::smoke_ventilation:
        return "smoke_ventilation";
    case networks::NetworkKind::water:
        return "water";
    case networks::NetworkKind::logistics:
        return "logistics";
    }
    return "unknown";
}

[[nodiscard]] core::Result<networks::NetworkKind> parse_network_kind(std::string_view value) {
    if (value == "road") {
        return core::Result<networks::NetworkKind>::success(networks::NetworkKind::road);
    }
    if (value == "cart_access") {
        return core::Result<networks::NetworkKind>::success(networks::NetworkKind::cart_access);
    }
    if (value == "storage_access") {
        return core::Result<networks::NetworkKind>::success(networks::NetworkKind::storage_access);
    }
    if (value == "power") {
        return core::Result<networks::NetworkKind>::success(networks::NetworkKind::power);
    }
    if (value == "ward") {
        return core::Result<networks::NetworkKind>::success(networks::NetworkKind::ward);
    }
    if (value == "smoke_ventilation") {
        return core::Result<networks::NetworkKind>::success(
            networks::NetworkKind::smoke_ventilation);
    }
    if (value == "water") {
        return core::Result<networks::NetworkKind>::success(networks::NetworkKind::water);
    }
    if (value == "logistics") {
        return core::Result<networks::NetworkKind>::success(networks::NetworkKind::logistics);
    }
    return core::Result<networks::NetworkKind>::failure(
        "save_text.invalid_network_kind", "unknown network kind: " + std::string(value));
}

[[nodiscard]] core::Result<entities::EntityKind> parse_entity_kind(std::string_view value) {
    auto parsed = entities::entity_kind_from_name(value);
    if (parsed) {
        return parsed;
    }
    return core::Result<entities::EntityKind>::failure(
        "save_text.invalid_entity_kind", "unknown entity kind: " + std::string(value));
}

[[nodiscard]] std::string process_state_name(processes::ProcessState state) {
    switch (state) {
    case processes::ProcessState::running:
        return "running";
    case processes::ProcessState::interrupted:
        return "interrupted";
    case processes::ProcessState::complete:
        return "complete";
    }
    return "unknown";
}

[[nodiscard]] core::Result<processes::ProcessState> parse_process_state(std::string_view value) {
    if (value == "running") {
        return core::Result<processes::ProcessState>::success(processes::ProcessState::running);
    }
    if (value == "interrupted") {
        return core::Result<processes::ProcessState>::success(processes::ProcessState::interrupted);
    }
    if (value == "complete") {
        return core::Result<processes::ProcessState>::success(processes::ProcessState::complete);
    }
    return core::Result<processes::ProcessState>::failure(
        "save_text.invalid_process_state", "unknown process state: " + std::string(value));
}

[[nodiscard]] std::string_view
process_interruption_policy_name(processes::ProcessInterruptionPolicy policy) noexcept {
    switch (policy) {
    case processes::ProcessInterruptionPolicy::pause:
        return "pause";
    case processes::ProcessInterruptionPolicy::reset:
        return "reset";
    case processes::ProcessInterruptionPolicy::fail:
        return "fail";
    }
    return "unknown";
}

[[nodiscard]] std::optional<processes::ProcessInterruptionPolicy>
parse_process_interruption_policy(std::string_view value) noexcept {
    if (value == "pause")
        return processes::ProcessInterruptionPolicy::pause;
    if (value == "reset")
        return processes::ProcessInterruptionPolicy::reset;
    if (value == "fail")
        return processes::ProcessInterruptionPolicy::fail;
    return std::nullopt;
}

[[nodiscard]] std::string_view fire_state_name(simulation::FireState state) noexcept {
    switch (state) {
    case simulation::FireState::unlit:
        return "unlit";
    case simulation::FireState::lit:
        return "lit";
    case simulation::FireState::embers:
        return "embers";
    case simulation::FireState::out:
        return "out";
    }
    return "unknown";
}

[[nodiscard]] std::optional<simulation::FireState>
parse_fire_state(std::string_view value) noexcept {
    if (value == "unlit")
        return simulation::FireState::unlit;
    if (value == "lit")
        return simulation::FireState::lit;
    if (value == "embers")
        return simulation::FireState::embers;
    if (value == "out")
        return simulation::FireState::out;
    return std::nullopt;
}

[[nodiscard]] std::string encode_transform(const build::Transform& transform) {
    std::ostringstream output;
    output << transform.position.anchor.x << '~' << transform.position.anchor.y << '~'
           << transform.position.anchor.z << '~' << encode_double(transform.position.local_offset.x)
           << '~' << encode_double(transform.position.local_offset.y) << '~'
           << encode_double(transform.position.local_offset.z) << '~'
           << encode_double(transform.rotation_degrees.x) << '~'
           << encode_double(transform.rotation_degrees.y) << '~'
           << encode_double(transform.rotation_degrees.z) << '~' << encode_double(transform.scale.x)
           << '~' << encode_double(transform.scale.y) << '~' << encode_double(transform.scale.z);
    return output.str();
}

[[nodiscard]] std::string encode_world_position(const world::WorldPosition& value) {
    std::ostringstream output;
    output << value.anchor.x << '~' << value.anchor.y << '~' << value.anchor.z << '~'
           << encode_double(value.local_offset.x) << '~' << encode_double(value.local_offset.y)
           << '~' << encode_double(value.local_offset.z);
    return output.str();
}

[[nodiscard]] core::Result<world::WorldPosition> parse_world_position(std::string_view value,
                                                                      std::string_view label) {
    const auto parts = split(value, '~');
    if (parts.size() != 3 && parts.size() != 6) {
        return core::Result<world::WorldPosition>::failure(
            "save_text.invalid_world_position",
            std::string(label) + " must contain 3 legacy or 6 anchored numeric fields");
    }
    if (parts.size() == 3) {
        auto x = parse_double(parts[0], "x");
        auto y = parse_double(parts[1], "y");
        auto z = parse_double(parts[2], "z");
        if (!x || !y || !z) {
            return core::Result<world::WorldPosition>::failure(
                "save_text.invalid_world_position",
                std::string(label) + " contains invalid legacy numbers");
        }
        return world::WorldPosition::from_legacy_global({x.value(), y.value(), z.value()});
    }

    auto ax = parse_i64(parts[0], "anchor_x");
    auto ay = parse_i64(parts[1], "anchor_y");
    auto az = parse_i64(parts[2], "anchor_z");
    auto lx = parse_double(parts[3], "local_x");
    auto ly = parse_double(parts[4], "local_y");
    auto lz = parse_double(parts[5], "local_z");
    if (!ax || !ay || !az || !lx || !ly || !lz) {
        return core::Result<world::WorldPosition>::failure(
            "save_text.invalid_world_position",
            std::string(label) + " contains invalid anchored numbers");
    }
    return world::WorldPosition::from_anchor({ax.value(), ay.value(), az.value()},
                                             {lx.value(), ly.value(), lz.value()});
}

[[nodiscard]] core::Result<build::Transform> parse_transform(std::string_view value) {
    const auto parts = split(value, '~');
    if (parts.size() != 9 && parts.size() != 12) {
        return core::Result<build::Transform>::failure(
            "save_text.invalid_transform",
            "transform must contain 9 legacy or 12 anchored numeric fields");
    }

    build::Transform transform;
    const auto spatial_field_count = parts.size() == 12 ? std::size_t{6} : std::size_t{3};
    const auto position_text = [&parts, spatial_field_count] {
        std::ostringstream encoded;
        for (std::size_t index = 0; index < spatial_field_count; ++index) {
            if (index > 0) {
                encoded << '~';
            }
            encoded << parts[index];
        }
        return encoded.str();
    }();
    auto position = parse_world_position(position_text, "transform_position");
    auto rx = parse_double(parts[spatial_field_count], "rotation_x");
    auto ry = parse_double(parts[spatial_field_count + 1], "rotation_y");
    auto rz = parse_double(parts[spatial_field_count + 2], "rotation_z");
    auto sx = parse_double(parts[spatial_field_count + 3], "scale_x");
    auto sy = parse_double(parts[spatial_field_count + 4], "scale_y");
    auto sz = parse_double(parts[spatial_field_count + 5], "scale_z");
    if (!position || !rx || !ry || !rz || !sx || !sy || !sz) {
        return core::Result<build::Transform>::failure("save_text.invalid_transform",
                                                       "build transform contains invalid numbers");
    }
    transform.position = position.value();
    transform.rotation_degrees = {rx.value(), ry.value(), rz.value()};
    transform.scale = {sx.value(), sy.value(), sz.value()};
    return core::Result<build::Transform>::success(transform);
}

[[nodiscard]] std::string encode_sockets(const std::vector<build::BuildSocket>& sockets) {
    std::ostringstream output;
    for (std::size_t index = 0; index < sockets.size(); ++index) {
        if (index > 0) {
            output << ',';
        }
        const auto& socket = sockets[index];
        output << percent_escape(socket.name) << '~' << encode_double(socket.local_position.x)
               << '~' << encode_double(socket.local_position.y) << '~'
               << encode_double(socket.local_position.z) << '~' << percent_escape(socket.tag);
    }
    return output.str();
}

[[nodiscard]] core::Result<std::vector<build::BuildSocket>> parse_sockets(std::string_view value) {
    std::vector<build::BuildSocket> sockets;
    if (value.empty()) {
        return core::Result<std::vector<build::BuildSocket>>::success(std::move(sockets));
    }
    for (const auto entry : split(value, ',')) {
        const auto parts = split(entry, '~');
        if (parts.size() != 5) {
            return core::Result<std::vector<build::BuildSocket>>::failure(
                "save_text.invalid_socket", "build socket record must contain 5 fields");
        }
        auto name = percent_unescape(parts[0]);
        auto x = parse_double(parts[1], "socket_x");
        auto y = parse_double(parts[2], "socket_y");
        auto z = parse_double(parts[3], "socket_z");
        auto tag = percent_unescape(parts[4]);
        if (!name || !x || !y || !z || !tag) {
            return core::Result<std::vector<build::BuildSocket>>::failure(
                "save_text.invalid_socket", "build socket contains invalid fields");
        }
        sockets.push_back(build::BuildSocket{
            std::move(name).value(), {x.value(), y.value(), z.value()}, std::move(tag).value()});
    }
    return core::Result<std::vector<build::BuildSocket>>::success(std::move(sockets));
}

[[nodiscard]] std::string encode_build_ports(const std::vector<build::BuildNetworkPort>& ports) {
    std::ostringstream output;
    for (std::size_t index = 0; index < ports.size(); ++index) {
        if (index > 0) {
            output << ',';
        }
        const auto& port = ports[index];
        output << percent_escape(port.name) << '~' << save_network_kind_name(port.kind) << '~'
               << port.capacity;
    }
    return output.str();
}

[[nodiscard]] core::Result<std::vector<build::BuildNetworkPort>>
parse_build_ports(std::string_view value) {
    std::vector<build::BuildNetworkPort> ports;
    if (value.empty()) {
        return core::Result<std::vector<build::BuildNetworkPort>>::success(std::move(ports));
    }
    for (const auto entry : split(value, ',')) {
        const auto parts = split(entry, '~');
        if (parts.size() != 3) {
            return core::Result<std::vector<build::BuildNetworkPort>>::failure(
                "save_text.invalid_build_port", "build network port record must contain 3 fields");
        }
        auto name = percent_unescape(parts[0]);
        auto kind = parse_network_kind(parts[1]);
        auto capacity = parse_u32(parts[2], "build_port_capacity");
        if (!name || !kind || !capacity) {
            return core::Result<std::vector<build::BuildNetworkPort>>::failure(
                "save_text.invalid_build_port", "build network port contains invalid fields");
        }
        ports.push_back(
            build::BuildNetworkPort{std::move(name).value(), kind.value(), capacity.value()});
    }
    return core::Result<std::vector<build::BuildNetworkPort>>::success(std::move(ports));
}

[[nodiscard]] std::string encode_item_stacks(const std::vector<items::ItemStack>& stacks) {
    std::ostringstream output;
    for (std::size_t index = 0; index < stacks.size(); ++index) {
        if (index > 0) {
            output << ',';
        }
        const auto& stack = stacks[index];
        output << percent_escape(stack.prototype_id.value()) << '~' << stack.count << '~'
               << stack.max_count << '~' << stack.quality;
    }
    return output.str();
}

[[nodiscard]] core::Result<std::vector<items::ItemStack>>
parse_item_stacks(std::string_view value) {
    std::vector<items::ItemStack> stacks;
    if (value.empty()) {
        return core::Result<std::vector<items::ItemStack>>::success(std::move(stacks));
    }
    for (const auto entry : split(value, ',')) {
        const auto parts = split(entry, '~');
        if (parts.size() != 4) {
            return core::Result<std::vector<items::ItemStack>>::failure(
                "save_text.invalid_item_stack", "item stack record must contain 4 fields");
        }
        auto prototype = parse_prototype_id(parts[0]);
        auto count = parse_u32(parts[1], "item_count");
        auto max_count = parse_u32(parts[2], "item_max_count");
        auto quality = parse_u16(parts[3], "item_quality");
        if (!prototype || !count || !max_count || !quality) {
            return core::Result<std::vector<items::ItemStack>>::failure(
                "save_text.invalid_item_stack", "item stack contains invalid fields");
        }
        stacks.push_back(
            items::ItemStack{prototype.value(), count.value(), max_count.value(), quality.value()});
    }
    return core::Result<std::vector<items::ItemStack>>::success(std::move(stacks));
}

[[nodiscard]] std::string encode_process_slots(const std::vector<processes::ProcessSlot>& slots) {
    std::ostringstream output;
    for (std::size_t index = 0; index < slots.size(); ++index) {
        if (index > 0) {
            output << ',';
        }
        output << percent_escape(slots[index].prototype_id.value()) << '~' << slots[index].count;
    }
    return output.str();
}

[[nodiscard]] core::Result<std::vector<processes::ProcessSlot>>
parse_process_slots(std::string_view value) {
    std::vector<processes::ProcessSlot> slots;
    if (value.empty()) {
        return core::Result<std::vector<processes::ProcessSlot>>::success(std::move(slots));
    }
    for (const auto entry : split(value, ',')) {
        const auto parts = split(entry, '~');
        if (parts.size() != 2) {
            return core::Result<std::vector<processes::ProcessSlot>>::failure(
                "save_text.invalid_process_slot", "process slot record must contain 2 fields");
        }
        auto prototype = parse_prototype_id(parts[0]);
        auto count = parse_u32(parts[1], "process_slot_count");
        if (!prototype || !count) {
            return core::Result<std::vector<processes::ProcessSlot>>::failure(
                "save_text.invalid_process_slot", "process slot contains invalid fields");
        }
        slots.push_back(processes::ProcessSlot{prototype.value(), count.value()});
    }
    return core::Result<std::vector<processes::ProcessSlot>>::success(std::move(slots));
}

[[nodiscard]] std::string
encode_assembly_parts(const std::vector<assemblies::AssemblyPart>& parts) {
    std::ostringstream output;
    for (std::size_t index = 0; index < parts.size(); ++index) {
        if (index > 0) {
            output << ',';
        }
        const auto& part = parts[index];
        output << percent_escape(part.name) << '~' << part.build_piece_id.value() << '~'
               << percent_escape(part.prototype_id.value()) << '~' << part.relative_coord.x << '~'
               << part.relative_coord.y << '~' << part.relative_coord.z;
    }
    return output.str();
}

[[nodiscard]] core::Result<std::vector<assemblies::AssemblyPart>>
parse_assembly_parts(std::string_view value) {
    std::vector<assemblies::AssemblyPart> parts_result;
    if (value.empty()) {
        return core::Result<std::vector<assemblies::AssemblyPart>>::success(
            std::move(parts_result));
    }
    for (const auto entry : split(value, ',')) {
        const auto parts = split(entry, '~');
        if (parts.size() != 3 && parts.size() != 6) {
            return core::Result<std::vector<assemblies::AssemblyPart>>::failure(
                "save_text.invalid_assembly_part",
                "assembly part record must contain 3 or 6 fields");
        }
        auto name = percent_unescape(parts[0]);
        auto build_piece_id = parse_u64(parts[1], "assembly_part_build_piece_id");
        auto prototype = parse_prototype_id(parts[2]);
        if (!name || !build_piece_id || !prototype) {
            return core::Result<std::vector<assemblies::AssemblyPart>>::failure(
                "save_text.invalid_assembly_part", "assembly part contains invalid fields");
        }
        world::BlockCoord relative{};
        if (parts.size() == 6) {
            auto x = parse_i64(parts[3], "assembly_part_relative_x");
            auto y = parse_i64(parts[4], "assembly_part_relative_y");
            auto z = parse_i64(parts[5], "assembly_part_relative_z");
            if (!x || !y || !z)
                return core::Result<std::vector<assemblies::AssemblyPart>>::failure(
                    "save_text.invalid_assembly_part", "assembly part position is invalid");
            relative = {x.value(), y.value(), z.value()};
        }
        parts_result.push_back(assemblies::AssemblyPart{
            std::move(name).value(), core::SaveId::from_value(build_piece_id.value()),
            prototype.value(), relative});
    }
    return core::Result<std::vector<assemblies::AssemblyPart>>::success(std::move(parts_result));
}

[[nodiscard]] std::string
encode_assembly_ports(const std::vector<assemblies::AssemblyPort>& ports) {
    std::ostringstream output;
    for (std::size_t index = 0; index < ports.size(); ++index) {
        if (index > 0) {
            output << ',';
        }
        output << percent_escape(ports[index].name) << '~'
               << save_network_kind_name(ports[index].kind) << '~'
               << ports[index].source_build_piece_id.value() << '~' << ports[index].capacity << '~'
               << ports[index].relative_coord.x << '~' << ports[index].relative_coord.y << '~'
               << ports[index].relative_coord.z;
    }
    return output.str();
}

[[nodiscard]] core::Result<std::vector<assemblies::AssemblyPort>>
parse_assembly_ports(std::string_view value) {
    std::vector<assemblies::AssemblyPort> ports;
    if (value.empty()) {
        return core::Result<std::vector<assemblies::AssemblyPort>>::success(std::move(ports));
    }
    for (const auto entry : split(value, ',')) {
        const auto parts = split(entry, '~');
        if (parts.size() != 2 && parts.size() != 4 && parts.size() != 7) {
            return core::Result<std::vector<assemblies::AssemblyPort>>::failure(
                "save_text.invalid_assembly_port",
                "assembly port record must contain 2 or 4 fields");
        }
        auto name = percent_unescape(parts[0]);
        auto kind = parse_network_kind(parts[1]);
        if (!name || !kind) {
            return core::Result<std::vector<assemblies::AssemblyPort>>::failure(
                "save_text.invalid_assembly_port", "assembly port contains invalid fields");
        }
        core::SaveId source_build_piece_id;
        std::uint32_t capacity = 1;
        world::BlockCoord relative{};
        if (parts.size() >= 4) {
            auto source = parse_u64(parts[2], "assembly_port_source_build_piece_id");
            auto parsed_capacity = parse_u32(parts[3], "assembly_port_capacity");
            if (!source || !parsed_capacity) {
                return core::Result<std::vector<assemblies::AssemblyPort>>::failure(
                    "save_text.invalid_assembly_port",
                    "assembly port contains invalid source or capacity fields");
            }
            source_build_piece_id = core::SaveId::from_value(source.value());
            capacity = parsed_capacity.value();
        }
        if (parts.size() == 7) {
            auto x = parse_i64(parts[4], "assembly_port_relative_x");
            auto y = parse_i64(parts[5], "assembly_port_relative_y");
            auto z = parse_i64(parts[6], "assembly_port_relative_z");
            if (!x || !y || !z)
                return core::Result<std::vector<assemblies::AssemblyPort>>::failure(
                    "save_text.invalid_assembly_port", "assembly port position is invalid");
            relative = {x.value(), y.value(), z.value()};
        }
        ports.push_back(assemblies::AssemblyPort{std::move(name).value(), kind.value(),
                                                 source_build_piece_id, capacity, relative});
    }
    return core::Result<std::vector<assemblies::AssemblyPort>>::success(std::move(ports));
}

[[nodiscard]] std::string encode_process_ids(const std::vector<core::ProcessId>& values) {
    std::ostringstream output;
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0)
            output << ',';
        output << values[index].value();
    }
    return output.str();
}

[[nodiscard]] core::Result<std::vector<core::ProcessId>> parse_process_ids(std::string_view value) {
    std::vector<core::ProcessId> result;
    if (value.empty())
        return core::Result<std::vector<core::ProcessId>>::success(std::move(result));
    for (const auto entry : split(value, ',')) {
        auto parsed = parse_u64(entry, "assembly_process_slot");
        if (!parsed || parsed.value() == 0) {
            return core::Result<std::vector<core::ProcessId>>::failure(
                "save_text.invalid_assembly_process_slot",
                "assembly process slot contains an invalid process id");
        }
        result.push_back(core::ProcessId::from_value(parsed.value()));
    }
    return core::Result<std::vector<core::ProcessId>>::success(std::move(result));
}

} // namespace

std::string SaveTextCodec::encode_metadata(const SaveMetadata& metadata) {
    std::ostringstream output;
    output << magic << '\n';
    output << "schema_version=" << metadata.schema_version << '\n';
    output << "game_version=" << percent_escape(metadata.game_version) << '\n';
    output << "world_seed=" << metadata.world_seed << '\n';
    output << "world_time=" << metadata.world_time << '\n';

    for (const auto& mod : metadata.enabled_mods) {
        output << "mod=" << percent_escape(mod.id) << '|' << percent_escape(mod.version) << '|'
               << percent_escape(mod.prototype_hash) << '\n';
    }

    for (const auto& migration : metadata.migration_history) {
        output << "migration=" << percent_escape(migration) << '\n';
    }

    output << "end\n";
    return output.str();
}

core::Result<SaveMetadata> SaveTextCodec::decode_metadata(std::string_view text) {
    SaveMetadata metadata;
    bool saw_magic = false;
    bool saw_end = false;
    bool saw_schema = false;
    bool saw_game_version = false;
    bool saw_world_seed = false;
    bool saw_world_time = false;

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
                return core::Result<SaveMetadata>::failure(
                    "save_text.invalid_magic",
                    "save metadata does not start with the expected magic");
            }
            saw_magic = true;
        } else if (line == "end") {
            saw_end = true;
            break;
        } else if (!line.empty()) {
            const auto separator = line.find('=');
            if (separator == std::string_view::npos) {
                return core::Result<SaveMetadata>::failure(
                    "save_text.invalid_line", "save metadata line is missing key/value separator");
            }

            const auto key = line.substr(0, separator);
            const auto value = line.substr(separator + 1);

            if (key == "schema_version") {
                auto parsed = parse_u32(value, key);
                if (!parsed) {
                    return core::Result<SaveMetadata>::failure(parsed.error().code,
                                                               parsed.error().message);
                }
                metadata.schema_version = parsed.value();
                saw_schema = true;
            } else if (key == "game_version") {
                auto parsed = percent_unescape(value);
                if (!parsed) {
                    return core::Result<SaveMetadata>::failure(parsed.error().code,
                                                               parsed.error().message);
                }
                metadata.game_version = std::move(parsed).value();
                saw_game_version = true;
            } else if (key == "world_seed") {
                auto parsed = parse_u64(value, key);
                if (!parsed) {
                    return core::Result<SaveMetadata>::failure(parsed.error().code,
                                                               parsed.error().message);
                }
                metadata.world_seed = parsed.value();
                saw_world_seed = true;
            } else if (key == "world_time") {
                auto parsed = parse_u64(value, key);
                if (!parsed) {
                    return core::Result<SaveMetadata>::failure(parsed.error().code,
                                                               parsed.error().message);
                }
                metadata.world_time = parsed.value();
                saw_world_time = true;
            } else if (key == "mod") {
                auto parsed = parse_mod_record(value);
                if (!parsed) {
                    return core::Result<SaveMetadata>::failure(parsed.error().code,
                                                               parsed.error().message);
                }
                metadata.enabled_mods.push_back(std::move(parsed).value());
            } else if (key == "migration") {
                auto parsed = percent_unescape(value);
                if (!parsed) {
                    return core::Result<SaveMetadata>::failure(parsed.error().code,
                                                               parsed.error().message);
                }
                metadata.migration_history.push_back(std::move(parsed).value());
            } else {
                return core::Result<SaveMetadata>::failure(
                    "save_text.unknown_key", "unknown save metadata key: " + std::string(key));
            }
        }

        if (line_end == std::string_view::npos) {
            break;
        }
        line_start = line_end + 1;
    }

    if (!saw_magic || !saw_end || !saw_schema || !saw_game_version || !saw_world_seed ||
        (metadata.schema_version >= 2 && !saw_world_time)) {
        return core::Result<SaveMetadata>::failure("save_text.incomplete_metadata",
                                                   "save metadata is missing required fields");
    }

    auto status = metadata.validate();
    if (!status) {
        return core::Result<SaveMetadata>::failure(status.error().code, status.error().message);
    }

    return core::Result<SaveMetadata>::success(std::move(metadata));
}

std::string SaveTextCodec::encode_snapshot(const SaveSnapshot& snapshot) {
    std::ostringstream output;
    output << snapshot_magic_v2 << '\n';
    output << "schema_version=" << snapshot.metadata.schema_version << '\n';
    output << "game_version=" << percent_escape(snapshot.metadata.game_version) << '\n';
    output << "world_seed=" << snapshot.metadata.world_seed << '\n';
    output << "world_time=" << snapshot.metadata.world_time << '\n';

    for (const auto& mod : snapshot.metadata.enabled_mods) {
        output << "mod=" << percent_escape(mod.id) << '|' << percent_escape(mod.version) << '|'
               << percent_escape(mod.prototype_hash) << '\n';
    }
    for (const auto& migration : snapshot.metadata.migration_history) {
        output << "migration=" << percent_escape(migration) << '\n';
    }
    for (const auto& entry : snapshot.voxel_palette.entries) {
        output << "voxel_palette=" << entry.type << '|'
               << percent_escape(entry.prototype_id.value()) << '\n';
    }

    for (const auto& chunk : snapshot.chunk_edits) {
        output << "chunk=" << chunk.coord.x << '|' << chunk.coord.y << '|' << chunk.coord.z << '|'
               << percent_escape(chunk.encoded_edit_delta) << '\n';
    }
    for (const auto& build_piece : snapshot.build_pieces) {
        output << "build=" << build_piece.object_id.value() << '|'
               << percent_escape(build_piece.prototype_id.value()) << '|'
               << encode_transform(build_piece.transform) << '|'
               << construction_state_name(build_piece.construction_state) << '|'
               << encode_sockets(build_piece.sockets) << '|'
               << encode_build_ports(build_piece.network_ports) << '|'
               << encode_string_list(build_piece.material_tags) << '|'
               << encode_string_list(build_piece.room_contribution_tags) << '\n';
    }
    for (const auto& entity : snapshot.entities) {
        output << "entity=" << entity.save_id.value() << '|'
               << percent_escape(entity.prototype_id.value()) << '|'
               << entities::entity_kind_name(entity.kind) << '|'
               << encode_transform(entity.transform) << '|' << (entity.sleeping ? '1' : '0') << '|'
               << percent_escape(entity.encoded_state) << '\n';
    }
    for (const auto& inventory : snapshot.inventories) {
        output << "inventory=" << inventory.owner_id.value() << '|'
               << encode_item_stacks(inventory.stacks) << '\n';
    }
    for (const auto& cargo_record : snapshot.cargo_records) {
        output << "cargo=" << cargo_record.cargo_id.value() << '|'
               << percent_escape(cargo_record.prototype_id.value()) << '|'
               << encode_world_position(cargo_record.position) << '|' << cargo_record.mass_grams
               << '|' << cargo_record.volume_milliliters << '|' << cargo_record.stability_per_mille
               << '|' << cargo_record.allowed_transport_modes.bits() << '|'
               << encode_string_list(cargo_record.hazard_tags) << '\n';
    }
    for (const auto& workpiece : snapshot.workpieces) {
        output << "workpiece=" << workpiece.workpiece_id.value() << '|'
               << percent_escape(workpiece.prototype_id.value()) << '|' << workpiece.shape.width
               << '|' << workpiece.shape.height << '|' << workpiece.shape.depth << '|'
               << percent_escape(workpiece.encoded_cells) << '|'
               << percent_escape(workpiece.material_prototype_id.value()) << '|'
               << percent_escape(workpiece.encoded_server_state) << '|'
               << workpiece.owner_session.value() << '|' << workpiece.revision << '|'
               << (workpiece.committed ? '1' : '0') << '\n';
    }
    for (const auto& assembly : snapshot.assemblies) {
        output << "assembly=" << assembly.assembly_id.value() << '|'
               << assembly.root_build_piece_id.value() << '|'
               << percent_escape(assembly.prototype_id.value()) << '|'
               << (assembly.operating ? '1' : '0') << '|' << encode_assembly_parts(assembly.parts)
               << '|' << encode_assembly_ports(assembly.ports) << '|'
               << assemblies::assembly_state_name(
                      assembly.operating ? assemblies::AssemblyState::operating : assembly.state)
               << '|' << assembly.current_stage << '|' << assembly.revision << '|'
               << encode_string_list(assembly.capabilities) << '|'
               << encode_process_ids(assembly.process_slots) << '|'
               << percent_escape(assembly.failure_reason) << '|'
               << percent_escape(assembly.custom_state) << '|' << assembly.root_coord.x << '|'
               << assembly.root_coord.y << '|' << assembly.root_coord.z << '\n';
    }
    for (const auto& process : snapshot.processes) {
        output << "process=" << process.process_id.value() << '|' << process.owner_id.value() << '|'
               << percent_escape(process.prototype_id.value()) << '|' << process.started_at << '|'
               << process.last_eval << '|' << process.required_work_ticks << '|'
               << process.accrued_work_ticks << '|' << process_state_name(process.state) << '|'
               << percent_escape(process.interruption_reason) << '|'
               << encode_process_slots(process.input_slots) << '|'
               << encode_process_slots(process.output_slots) << '|'
               << (process.output_claimed ? '1' : '0') << '|'
               << percent_escape(process.condition_function_id) << '|'
               << process_interruption_policy_name(process.interruption_policy) << '\n';
    }
    for (const auto& fire : snapshot.fires) {
        output << "fire=" << fire.fire_id.value() << '|'
               << percent_escape(fire.prototype_id.value()) << '|' << fire_state_name(fire.state)
               << '|' << fire.fuel_buffer_ticks << '|' << fire.last_eval << '|' << fire.embers_until
               << '|' << (fire.weather_exposed ? '1' : '0') << '\n';
    }
    for (const auto& mod_state : snapshot.mod_states) {
        output << "mod_state=" << percent_escape(mod_state.mod_id) << '|'
               << percent_escape(mod_state.state_key) << '|'
               << percent_escape(mod_state.encoded_state) << '\n';
    }
    for (const auto& missing : snapshot.missing_prototypes) {
        output << "missing_prototype=" << world::missing_prototype_kind_name(missing.kind) << '|'
               << missing.stable_id << '|' << percent_escape(missing.original_prototype_id.value())
               << '|' << encode_world_position(missing.position) << '|' << missing.owner_id.value()
               << '|' << percent_escape(missing.warning) << '|'
               << percent_escape(missing.saved_blob) << '\n';
    }

    output << "end\n";
    return output.str();
}

core::Result<SaveSnapshot> SaveTextCodec::decode_snapshot(std::string_view text) {
    constexpr std::size_t max_save_text_bytes = 512U * 1024U * 1024U;
    constexpr std::size_t max_save_records = 1'000'000U;
    constexpr std::size_t max_save_line_bytes = 16U * 1024U * 1024U;
    if (text.size() > max_save_text_bytes)
        return core::Result<SaveSnapshot>::failure("save_text.file_too_large",
                                                   "text save exceeds its safety limit");
    SaveSnapshot snapshot;
    bool saw_magic = false;
    bool saw_end = false;
    bool saw_schema = false;
    bool saw_game_version = false;
    bool saw_world_seed = false;
    bool saw_world_time = false;
    std::size_t consumed_bytes = 0;
    std::size_t record_count = 0;

    std::size_t line_start = 0;
    while (line_start <= text.size()) {
        const auto line_end = text.find('\n', line_start);
        auto line = line_end == std::string_view::npos
                        ? text.substr(line_start)
                        : text.substr(line_start, line_end - line_start);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        if (line.size() > max_save_line_bytes || ++record_count > max_save_records)
            return core::Result<SaveSnapshot>::failure(
                "save_text.limit_exceeded", "text save exceeds its line or record limit");

        if (!saw_magic) {
            if (line != snapshot_magic_v1 && line != snapshot_magic_v2) {
                return core::Result<SaveSnapshot>::failure(
                    "save_text.invalid_snapshot_magic",
                    "save snapshot does not start with the expected magic");
            }
            saw_magic = true;
        } else if (line == "end") {
            saw_end = true;
            consumed_bytes = line_end == std::string_view::npos ? text.size() : line_end + 1U;
            break;
        } else if (!line.empty()) {
            const auto separator = line.find('=');
            if (separator == std::string_view::npos) {
                return core::Result<SaveSnapshot>::failure(
                    "save_text.invalid_line", "save snapshot line is missing key/value separator");
            }

            const auto key = line.substr(0, separator);
            const auto value = line.substr(separator + 1);

            if (key == "schema_version") {
                auto parsed = parse_u32(value, key);
                if (!parsed) {
                    return core::Result<SaveSnapshot>::failure(parsed.error().code,
                                                               parsed.error().message);
                }
                snapshot.metadata.schema_version = parsed.value();
                saw_schema = true;
            } else if (key == "game_version") {
                auto parsed = percent_unescape(value);
                if (!parsed) {
                    return core::Result<SaveSnapshot>::failure(parsed.error().code,
                                                               parsed.error().message);
                }
                snapshot.metadata.game_version = std::move(parsed).value();
                saw_game_version = true;
            } else if (key == "world_seed") {
                auto parsed = parse_u64(value, key);
                if (!parsed) {
                    return core::Result<SaveSnapshot>::failure(parsed.error().code,
                                                               parsed.error().message);
                }
                snapshot.metadata.world_seed = parsed.value();
                saw_world_seed = true;
            } else if (key == "world_time") {
                auto parsed = parse_u64(value, key);
                if (!parsed) {
                    return core::Result<SaveSnapshot>::failure(parsed.error().code,
                                                               parsed.error().message);
                }
                snapshot.metadata.world_time = parsed.value();
                saw_world_time = true;
            } else if (key == "mod") {
                auto parsed = parse_mod_record(value);
                if (!parsed) {
                    return core::Result<SaveSnapshot>::failure(parsed.error().code,
                                                               parsed.error().message);
                }
                snapshot.metadata.enabled_mods.push_back(std::move(parsed).value());
            } else if (key == "migration") {
                auto parsed = percent_unescape(value);
                if (!parsed) {
                    return core::Result<SaveSnapshot>::failure(parsed.error().code,
                                                               parsed.error().message);
                }
                snapshot.metadata.migration_history.push_back(std::move(parsed).value());
            } else if (key == "voxel_palette") {
                const auto parts = split(value, '|');
                if (parts.size() != 2) {
                    return core::Result<SaveSnapshot>::failure(
                        "save_text.invalid_voxel_palette",
                        "voxel palette entry must contain type and prototype id");
                }
                auto type = parse_u16(parts[0], "voxel_palette_type");
                auto encoded_id = percent_unescape(parts[1]);
                if (!type || !encoded_id) {
                    return core::Result<SaveSnapshot>::failure(
                        "save_text.invalid_voxel_palette",
                        "voxel palette entry contains invalid fields");
                }
                auto prototype_id = core::PrototypeId::parse(encoded_id.value());
                if (!prototype_id) {
                    return core::Result<SaveSnapshot>::failure(
                        "save_text.invalid_voxel_palette",
                        "voxel palette entry prototype id is invalid");
                }
                snapshot.voxel_palette.entries.push_back({type.value(), prototype_id.value()});
            } else if (key == "chunk") {
                const auto parts = split(value, '|');
                if (parts.size() != 4) {
                    return core::Result<SaveSnapshot>::failure(
                        "save_text.invalid_chunk", "chunk record must contain 4 fields");
                }
                auto x = parse_i64(parts[0], "chunk_x");
                auto y = parse_i64(parts[1], "chunk_y");
                auto z = parse_i64(parts[2], "chunk_z");
                auto delta = percent_unescape(parts[3]);
                if (!x || !y || !z || !delta) {
                    return core::Result<SaveSnapshot>::failure(
                        "save_text.invalid_chunk", "chunk record contains invalid fields");
                }
                snapshot.chunk_edits.push_back(
                    {{x.value(), y.value(), z.value()}, std::move(delta).value()});
            } else if (key == "build") {
                const auto parts = split(value, '|');
                if (parts.size() != 8) {
                    return core::Result<SaveSnapshot>::failure(
                        "save_text.invalid_build", "build record must contain 8 fields");
                }
                auto id = parse_u64(parts[0], "build_id");
                auto prototype = parse_prototype_id(parts[1]);
                auto transform = parse_transform(parts[2]);
                auto state = parse_construction_state(parts[3]);
                auto sockets = parse_sockets(parts[4]);
                auto ports = parse_build_ports(parts[5]);
                auto materials = parse_string_list(parts[6]);
                auto room_tags = parse_string_list(parts[7]);
                if (!id || !prototype || !transform || !state || !sockets || !ports || !materials ||
                    !room_tags) {
                    return core::Result<SaveSnapshot>::failure(
                        "save_text.invalid_build", "build record contains invalid fields");
                }
                build::BuildPieceRecord record;
                record.object_id = core::SaveId::from_value(id.value());
                record.prototype_id = prototype.value();
                record.transform = transform.value();
                record.construction_state = state.value();
                record.sockets = std::move(sockets).value();
                record.network_ports = std::move(ports).value();
                record.material_tags = std::move(materials).value();
                record.room_contribution_tags = std::move(room_tags).value();
                snapshot.build_pieces.push_back(std::move(record));
            } else if (key == "entity") {
                const auto parts = split(value, '|');
                if (parts.size() != 5 && parts.size() != 6) {
                    return core::Result<SaveSnapshot>::failure(
                        "save_text.invalid_entity", "entity record must contain 5 or 6 fields");
                }
                auto id = parse_u64(parts[0], "entity_id");
                auto prototype = parse_prototype_id(parts[1]);
                auto kind = parse_entity_kind(parts[2]);
                build::Transform transform;
                std::size_t sleeping_index = 3;
                if (parts.size() == 6) {
                    auto parsed_transform = parse_transform(parts[3]);
                    if (!parsed_transform) {
                        return core::Result<SaveSnapshot>::failure(
                            parsed_transform.error().code, parsed_transform.error().message);
                    }
                    transform = parsed_transform.value();
                    sleeping_index = 4;
                }
                auto sleeping = parse_bool(parts[sleeping_index], "entity_sleeping");
                auto encoded_state = percent_unescape(parts[sleeping_index + 1]);
                if (!id || !prototype || !kind || !sleeping || !encoded_state) {
                    return core::Result<SaveSnapshot>::failure(
                        "save_text.invalid_entity", "entity record contains invalid fields");
                }
                snapshot.entities.push_back({core::SaveId::from_value(id.value()),
                                             prototype.value(), kind.value(), sleeping.value(),
                                             std::move(encoded_state).value(), transform});
            } else if (key == "inventory") {
                const auto parts = split(value, '|');
                if (parts.size() != 2) {
                    return core::Result<SaveSnapshot>::failure(
                        "save_text.invalid_inventory", "inventory record must contain 2 fields");
                }
                auto owner = parse_u64(parts[0], "inventory_owner");
                auto stacks = parse_item_stacks(parts[1]);
                if (!owner || !stacks) {
                    return core::Result<SaveSnapshot>::failure(
                        "save_text.invalid_inventory", "inventory record contains invalid fields");
                }
                snapshot.inventories.push_back(
                    {core::SaveId::from_value(owner.value()), std::move(stacks).value()});
            } else if (key == "cargo") {
                const auto parts = split(value, '|');
                if (parts.size() != 7 && parts.size() != 8) {
                    return core::Result<SaveSnapshot>::failure(
                        "save_text.invalid_cargo", "cargo record must contain 7 or 8 fields");
                }
                auto id = parse_u64(parts[0], "cargo_id");
                auto prototype = parse_prototype_id(parts[1]);
                world::WorldPosition position;
                std::size_t mass_index = 2;
                if (parts.size() == 8) {
                    auto parsed_position = parse_world_position(parts[2], "cargo_position");
                    if (!parsed_position) {
                        return core::Result<SaveSnapshot>::failure(parsed_position.error().code,
                                                                   parsed_position.error().message);
                    }
                    position = parsed_position.value();
                    mass_index = 3;
                }
                auto mass = parse_u64(parts[mass_index], "cargo_mass");
                auto volume = parse_u64(parts[mass_index + 1], "cargo_volume");
                auto stability = parse_i32(parts[mass_index + 2], "cargo_stability");
                auto transport = parse_u32(parts[mass_index + 3], "cargo_transport");
                auto hazards = parse_string_list(parts[mass_index + 4]);
                if (!id || !prototype || !mass || !volume || !stability || !transport || !hazards) {
                    return core::Result<SaveSnapshot>::failure(
                        "save_text.invalid_cargo", "cargo record contains invalid fields");
                }
                cargo::CargoRecord record;
                record.cargo_id = core::SaveId::from_value(id.value());
                record.prototype_id = prototype.value();
                record.position = position;
                record.mass_grams = mass.value();
                record.volume_milliliters = volume.value();
                record.stability_per_mille = stability.value();
                record.allowed_transport_modes =
                    cargo::CargoTransportModes::from_bits(transport.value());
                record.hazard_tags = std::move(hazards).value();
                snapshot.cargo_records.push_back(std::move(record));
            } else if (key == "workpiece") {
                const auto parts = split(value, '|');
                if (parts.size() != 6 && parts.size() != 11) {
                    return core::Result<SaveSnapshot>::failure(
                        "save_text.invalid_workpiece",
                        "workpiece record must contain 6 legacy or 11 rich fields");
                }
                auto id = parse_u64(parts[0], "workpiece_id");
                auto prototype = parse_prototype_id(parts[1]);
                auto x = parse_u16(parts[2], "workpiece_x");
                auto y = parse_u16(parts[3], "workpiece_y");
                auto z = parse_u16(parts[4], "workpiece_z");
                auto cells = percent_unescape(parts[5]);
                core::PrototypeId material;
                std::string server_state;
                core::NetId owner;
                std::uint64_t revision = 1;
                bool committed = false;
                if (parts.size() == 11) {
                    auto encoded_material = percent_unescape(parts[6]);
                    auto encoded_server_state = percent_unescape(parts[7]);
                    auto parsed_owner = parse_u64(parts[8], "workpiece_owner");
                    auto parsed_revision = parse_u64(parts[9], "workpiece_revision");
                    auto parsed_committed = parse_bool(parts[10], "workpiece_committed");
                    if (!encoded_material || !encoded_server_state || !parsed_owner ||
                        !parsed_revision || !parsed_committed) {
                        return core::Result<SaveSnapshot>::failure(
                            "save_text.invalid_workpiece",
                            "workpiece rich state contains invalid fields");
                    }
                    if (!encoded_material.value().empty()) {
                        auto parsed_material = core::PrototypeId::parse(encoded_material.value());
                        if (!parsed_material) {
                            return core::Result<SaveSnapshot>::failure(
                                "save_text.invalid_workpiece",
                                "workpiece material prototype id is invalid");
                        }
                        material = *parsed_material;
                    }
                    server_state = std::move(encoded_server_state).value();
                    owner = core::NetId::from_value(parsed_owner.value());
                    revision = parsed_revision.value();
                    committed = parsed_committed.value();
                }
                if (!id || !prototype || !x || !y || !z || !cells) {
                    return core::Result<SaveSnapshot>::failure(
                        "save_text.invalid_workpiece", "workpiece record contains invalid fields");
                }
                snapshot.workpieces.push_back({core::WorkpieceId::from_value(id.value()),
                                               prototype.value(),
                                               {x.value(), y.value(), z.value()},
                                               std::move(cells).value(),
                                               material,
                                               std::move(server_state),
                                               owner,
                                               revision,
                                               committed});
            } else if (key == "assembly") {
                const auto parts = split(value, '|');
                if (parts.size() != 6 && parts.size() != 13 && parts.size() != 16) {
                    return core::Result<SaveSnapshot>::failure(
                        "save_text.invalid_assembly",
                        "assembly record must contain 6 legacy or 13 state-machine fields");
                }
                auto id = parse_u64(parts[0], "assembly_id");
                auto root = parse_u64(parts[1], "assembly_root");
                auto prototype = parse_prototype_id(parts[2]);
                auto operating = parse_bool(parts[3], "assembly_operating");
                auto parts_result = parse_assembly_parts(parts[4]);
                auto ports = parse_assembly_ports(parts[5]);
                if (!id || !root || !prototype || !operating || !parts_result || !ports) {
                    return core::Result<SaveSnapshot>::failure(
                        "save_text.invalid_assembly", "assembly record contains invalid fields");
                }
                assemblies::AssemblyRecord record;
                record.assembly_id = core::SaveId::from_value(id.value());
                record.root_build_piece_id = core::SaveId::from_value(root.value());
                record.prototype_id = prototype.value();
                record.parts = std::move(parts_result).value();
                record.ports = std::move(ports).value();
                record.operating = operating.value();
                record.state = operating.value() ? assemblies::AssemblyState::operating
                                                 : assemblies::AssemblyState::ready;
                if (parts.size() >= 13) {
                    auto state = assemblies::parse_assembly_state(parts[6]);
                    auto stage = parse_u32(parts[7], "assembly_current_stage");
                    auto revision = parse_u64(parts[8], "assembly_revision");
                    auto capabilities = parse_string_list(parts[9]);
                    auto process_slots = parse_process_ids(parts[10]);
                    auto failure = percent_unescape(parts[11]);
                    auto custom = percent_unescape(parts[12]);
                    if (!state || !stage || !revision || !capabilities || !process_slots ||
                        !failure || !custom) {
                        return core::Result<SaveSnapshot>::failure(
                            "save_text.invalid_assembly",
                            "assembly state-machine fields are invalid");
                    }
                    record.state = state.value();
                    record.current_stage = stage.value();
                    record.revision = revision.value();
                    record.capabilities = std::move(capabilities).value();
                    record.process_slots = std::move(process_slots).value();
                    record.failure_reason = std::move(failure).value();
                    record.custom_state = std::move(custom).value();
                }
                if (parts.size() == 16) {
                    auto x = parse_i64(parts[13], "assembly_root_x");
                    auto y = parse_i64(parts[14], "assembly_root_y");
                    auto z = parse_i64(parts[15], "assembly_root_z");
                    if (!x || !y || !z)
                        return core::Result<SaveSnapshot>::failure(
                            "save_text.invalid_assembly", "assembly root anchor is invalid");
                    record.root_coord = {x.value(), y.value(), z.value()};
                }
                snapshot.assemblies.push_back(std::move(record));
            } else if (key == "process") {
                const auto parts = split(value, '|');
                if (parts.size() != 11 && parts.size() != 14) {
                    return core::Result<SaveSnapshot>::failure(
                        "save_text.invalid_process",
                        "process record must contain 11 legacy or 14 lazy-process fields");
                }
                auto id = parse_u64(parts[0], "process_id");
                auto owner = parse_u64(parts[1], "process_owner");
                auto prototype = parse_prototype_id(parts[2]);
                auto start = parse_u64(parts[3], "process_start");
                auto last = parse_u64(parts[4], "process_last_update");
                auto required = parse_u64(parts[5], "process_required");
                auto accumulated = parse_u64(parts[6], "process_accumulated");
                auto state = parse_process_state(parts[7]);
                auto interruption_reason = percent_unescape(parts[8]);
                auto inputs = parse_process_slots(parts[9]);
                auto outputs = parse_process_slots(parts[10]);
                auto output_claimed = parts.size() == 14
                                          ? parse_bool(parts[11], "process_output_claimed")
                                          : core::Result<bool>::success(false);
                auto condition = parts.size() == 14 ? percent_unescape(parts[12])
                                                    : core::Result<std::string>::success({});
                auto interruption_policy =
                    parts.size() == 14 ? parse_process_interruption_policy(parts[13])
                                       : std::optional{processes::ProcessInterruptionPolicy::pause};
                if (!id || !owner || !prototype || !start || !last || !required || !accumulated ||
                    !state || !interruption_reason || !inputs || !outputs || !output_claimed ||
                    !condition || !interruption_policy) {
                    return core::Result<SaveSnapshot>::failure(
                        "save_text.invalid_process", "process record contains invalid fields");
                }
                processes::ProcessInstance process;
                process.process_id = core::ProcessId::from_value(id.value());
                process.owner_id = core::SaveId::from_value(owner.value());
                process.prototype_id = prototype.value();
                process.started_at = start.value();
                process.last_eval = last.value();
                process.required_work_ticks = required.value();
                process.accrued_work_ticks = accumulated.value();
                process.state = state.value();
                process.interruption_reason = std::move(interruption_reason).value();
                process.input_slots = std::move(inputs).value();
                process.output_slots = std::move(outputs).value();
                process.output_claimed = output_claimed.value();
                process.condition_function_id = std::move(condition).value();
                process.interruption_policy = *interruption_policy;
                snapshot.processes.push_back(std::move(process));
            } else if (key == "mod_state") {
                const auto parts = split(value, '|');
                if (parts.size() != 3) {
                    return core::Result<SaveSnapshot>::failure(
                        "save_text.invalid_mod_state", "mod state record must contain 3 fields");
                }
                auto mod_id = percent_unescape(parts[0]);
                auto state_key = percent_unescape(parts[1]);
                auto encoded_state = percent_unescape(parts[2]);
                if (!mod_id || !state_key || !encoded_state) {
                    return core::Result<SaveSnapshot>::failure(
                        "save_text.invalid_mod_state", "mod state record contains invalid fields");
                }
                snapshot.mod_states.push_back({std::move(mod_id).value(),
                                               std::move(state_key).value(),
                                               std::move(encoded_state).value()});
            } else if (key == "fire") {
                const auto parts = split(value, '|');
                if (parts.size() != 7) {
                    return core::Result<SaveSnapshot>::failure("save_text.invalid_fire",
                                                               "fire record must contain 7 fields");
                }
                auto id = parse_u64(parts[0], "fire_id");
                auto prototype = parse_prototype_id(parts[1]);
                auto state = parse_fire_state(parts[2]);
                auto fuel = parse_u64(parts[3], "fire_fuel");
                auto last_eval = parse_u64(parts[4], "fire_last_eval");
                auto embers_until = parse_u64(parts[5], "fire_embers_until");
                auto exposed = parse_bool(parts[6], "fire_weather_exposed");
                if (!id || !prototype || !state || !fuel || !last_eval || !embers_until ||
                    !exposed) {
                    return core::Result<SaveSnapshot>::failure(
                        "save_text.invalid_fire", "fire record contains invalid fields");
                }
                snapshot.fires.push_back({core::SaveId::from_value(id.value()), prototype.value(),
                                          *state, fuel.value(), last_eval.value(),
                                          embers_until.value(), exposed.value()});
            } else if (key == "missing_prototype") {
                const auto parts = split(value, '|');
                if (parts.size() != 7) {
                    return core::Result<SaveSnapshot>::failure(
                        "save_text.invalid_missing_prototype",
                        "missing prototype placeholder must contain 7 fields");
                }
                auto kind = world::missing_prototype_kind_from_name(parts[0]);
                auto stable_id = parse_u64(parts[1], "missing_prototype_id");
                auto prototype = parse_prototype_id(parts[2]);
                auto position = parse_world_position(parts[3], "missing_prototype_position");
                auto owner = parse_u64(parts[4], "missing_prototype_owner");
                auto warning = percent_unescape(parts[5]);
                auto blob = percent_unescape(parts[6]);
                if (!kind || !stable_id || !prototype || !position || !owner || !warning || !blob) {
                    return core::Result<SaveSnapshot>::failure(
                        "save_text.invalid_missing_prototype",
                        "missing prototype placeholder contains invalid fields");
                }
                snapshot.missing_prototypes.push_back(
                    {*kind, stable_id.value(), prototype.value(), position.value(),
                     core::SaveId::from_value(owner.value()), std::move(blob).value(),
                     std::move(warning).value()});
            } else {
                return core::Result<SaveSnapshot>::failure("save_text.unknown_snapshot_key",
                                                           "unknown save snapshot key: " +
                                                               std::string(key));
            }
        }

        if (line_end == std::string_view::npos) {
            break;
        }
        line_start = line_end + 1;
    }

    if (!saw_magic || !saw_end || !saw_schema || !saw_game_version || !saw_world_seed ||
        (snapshot.metadata.schema_version >= 2 && !saw_world_time)) {
        return core::Result<SaveSnapshot>::failure("save_text.incomplete_snapshot",
                                                   "save snapshot is missing required fields");
    }
    if (consumed_bytes != text.size())
        return core::Result<SaveSnapshot>::failure(
            "save_text.trailing_data", "save snapshot contains data after its end marker");

    auto status = snapshot.metadata.validate();
    if (!status) {
        return core::Result<SaveSnapshot>::failure(status.error().code, status.error().message);
    }
    status = snapshot.voxel_palette.validate();
    if (!status) {
        return core::Result<SaveSnapshot>::failure(status.error().code, status.error().message);
    }

    return core::Result<SaveSnapshot>::success(std::move(snapshot));
}

} // namespace heartstead::save
