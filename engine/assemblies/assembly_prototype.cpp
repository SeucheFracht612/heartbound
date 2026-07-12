#include "engine/assemblies/assembly_prototype.hpp"

#include "engine/modding/prototype_registry.hpp"

#include <charconv>
#include <cstdint>
#include <ranges>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace heartstead::assemblies {

namespace {

[[nodiscard]] const std::string* field(const modding::GenericPrototype& prototype,
                                       std::string_view key) {
    const auto found = prototype.fields.find(std::string(key));
    return found == prototype.fields.end() ? nullptr : &found->second;
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

[[nodiscard]] core::Result<std::vector<AssemblyPartRequirement>>
parse_part_requirements(const modding::GenericPrototype& prototype, std::string_view key,
                        bool required_field, bool optional_parts) {
    const auto* value = field(prototype, key);
    std::vector<AssemblyPartRequirement> requirements;
    if (value == nullptr || value->empty()) {
        if (required_field) {
            return core::Result<std::vector<AssemblyPartRequirement>>::failure(
                "assembly_prototype.missing_parts",
                "assembly prototype must declare " + std::string(key));
        }
        return core::Result<std::vector<AssemblyPartRequirement>>::success(std::move(requirements));
    }

    for (const auto entry : split(*value, ',')) {
        const auto separator = entry.find(':');
        if (separator == std::string_view::npos) {
            return core::Result<std::vector<AssemblyPartRequirement>>::failure(
                "assembly_prototype.invalid_part",
                std::string(key) + " entry must be name:prototype_id");
        }

        const auto name = entry.substr(0, separator);
        const auto prototype_text = entry.substr(separator + 1);
        if (!core::is_valid_local_id(name)) {
            return core::Result<std::vector<AssemblyPartRequirement>>::failure(
                "assembly_prototype.invalid_part_name",
                std::string(key) + " has invalid part name: " + std::string(name));
        }

        auto prototype_id = core::PrototypeId::parse(prototype_text);
        if (!prototype_id) {
            return core::Result<std::vector<AssemblyPartRequirement>>::failure(
                "assembly_prototype.invalid_part_prototype",
                std::string(key) + " has invalid part prototype id");
        }

        requirements.push_back(
            {std::string(name), std::move(prototype_id).value(), optional_parts});
    }

    return core::Result<std::vector<AssemblyPartRequirement>>::success(std::move(requirements));
}

[[nodiscard]] core::Result<std::vector<AssemblyPort>>
parse_required_ports(const modding::GenericPrototype& prototype) {
    const auto* value = field(prototype, "required_ports");
    std::vector<AssemblyPort> ports;
    if (value == nullptr || value->empty()) {
        return core::Result<std::vector<AssemblyPort>>::success(std::move(ports));
    }

    for (const auto port_name : split(*value, ',')) {
        if (!core::is_valid_local_id(port_name)) {
            return core::Result<std::vector<AssemblyPort>>::failure(
                "assembly_prototype.invalid_port",
                "required_ports contains invalid port name: " + std::string(port_name));
        }
        ports.push_back(
            {std::string(port_name), networks::network_kind_for_port_name(port_name), {}, 1, {}});
    }

    return core::Result<std::vector<AssemblyPort>>::success(std::move(ports));
}

[[nodiscard]] std::vector<std::string> parse_string_list(const modding::GenericPrototype& prototype,
                                                         std::string_view key) {
    const auto* value = field(prototype, key);
    if (value == nullptr || value->empty())
        return {};
    std::vector<std::string> result;
    for (const auto entry : split(*value, ','))
        result.emplace_back(entry);
    return result;
}

[[nodiscard]] core::Result<std::uint32_t>
parse_capacity(const modding::GenericPrototype& prototype) {
    const auto* value = field(prototype, "capacity");
    if (value == nullptr)
        return core::Result<std::uint32_t>::success(1);
    std::uint32_t parsed = 0;
    const auto [end, error] = std::from_chars(value->data(), value->data() + value->size(), parsed);
    if (error != std::errc{} || end != value->data() + value->size() || parsed == 0) {
        return core::Result<std::uint32_t>::failure("assembly_prototype.invalid_capacity",
                                                    "assembly capacity must be a positive u32");
    }
    return core::Result<std::uint32_t>::success(parsed);
}

[[nodiscard]] core::Result<bool> parse_bool(const modding::GenericPrototype& prototype,
                                            std::string_view key) {
    const auto* value = field(prototype, key);
    if (value == nullptr || *value == "false")
        return core::Result<bool>::success(false);
    if (*value == "true")
        return core::Result<bool>::success(true);
    return core::Result<bool>::failure("assembly_prototype.invalid_bool",
                                       std::string(key) + " must be true or false");
}

[[nodiscard]] core::Status apply_part_layouts(const modding::GenericPrototype& prototype,
                                              std::vector<AssemblyPartRequirement>& requirements) {
    const auto* value = field(prototype, "part_layouts");
    if (value == nullptr || value->empty())
        return core::Status::ok();
    for (const auto entry : split(*value, ',')) {
        const auto parts = split(entry, '~');
        if (parts.size() != 6) {
            return core::Status::failure("assembly_prototype.invalid_part_layout",
                                         "part layout must be name~x~y~z~stage~role");
        }
        const auto requirement =
            std::ranges::find_if(requirements, [name = parts[0]](const auto& candidate) {
                return candidate.name == name;
            });
        if (requirement == requirements.end()) {
            return core::Status::failure("assembly_prototype.unknown_part_layout",
                                         "part layout references an undeclared part");
        }
        std::int64_t x = 0;
        std::int64_t y = 0;
        std::int64_t z = 0;
        std::uint32_t stage = 0;
        const auto parse = [](std::string_view text, auto& output) {
            const auto [end, error] =
                std::from_chars(text.data(), text.data() + text.size(), output);
            return error == std::errc{} && end == text.data() + text.size();
        };
        if (!parse(parts[1], x) || !parse(parts[2], y) || !parse(parts[3], z) ||
            !parse(parts[4], stage)) {
            return core::Status::failure("assembly_prototype.invalid_part_layout",
                                         "part layout contains invalid coordinates or stage");
        }
        requirement->relative_coord = {x, y, z};
        requirement->construction_stage = stage;
        requirement->role = std::string(parts[5]);
    }
    return core::Status::ok();
}

[[nodiscard]] core::Status apply_port_layouts(const modding::GenericPrototype& prototype,
                                              std::vector<AssemblyPort>& ports) {
    const auto* value = field(prototype, "port_layouts");
    if (value == nullptr || value->empty())
        return core::Status::ok();
    for (const auto entry : split(*value, ',')) {
        const auto parts = split(entry, '~');
        if (parts.size() != 4)
            return core::Status::failure("assembly_prototype.invalid_port_layout",
                                         "port layout must be name~x~y~z");
        const auto port = std::ranges::find_if(
            ports, [name = parts[0]](const auto& candidate) { return candidate.name == name; });
        if (port == ports.end())
            return core::Status::failure("assembly_prototype.unknown_port_layout",
                                         "port layout references an undeclared port");
        const auto parse = [](std::string_view text, std::int64_t& output) {
            const auto [end, error] =
                std::from_chars(text.data(), text.data() + text.size(), output);
            return error == std::errc{} && end == text.data() + text.size();
        };
        if (!parse(parts[1], port->relative_coord.x) || !parse(parts[2], port->relative_coord.y) ||
            !parse(parts[3], port->relative_coord.z))
            return core::Status::failure("assembly_prototype.invalid_port_layout",
                                         "port layout contains invalid coordinates");
    }
    return core::Status::ok();
}

} // namespace

core::Result<AssemblyDefinition>
assembly_definition_from_prototype(const modding::GenericPrototype& prototype) {
    if (prototype.kind != modding::PrototypeKinds::assembly) {
        return core::Result<AssemblyDefinition>::failure("assembly_prototype.kind_mismatch",
                                                         "prototype is not an assembly");
    }
    if (!prototype.id.is_valid()) {
        return core::Result<AssemblyDefinition>::failure("assembly_prototype.invalid_id",
                                                         "assembly prototype id is invalid");
    }

    auto required_parts = parse_part_requirements(prototype, "required_parts", true, false);
    auto optional_parts = parse_part_requirements(prototype, "optional_parts", false, true);
    auto required_ports = parse_required_ports(prototype);
    auto capacity = parse_capacity(prototype);
    auto requires_heat = parse_bool(prototype, "requires_heat");
    auto requires_power = parse_bool(prototype, "requires_power");
    if (!required_parts) {
        return core::Result<AssemblyDefinition>::failure(required_parts.error().code,
                                                         required_parts.error().message);
    }
    if (!optional_parts) {
        return core::Result<AssemblyDefinition>::failure(optional_parts.error().code,
                                                         optional_parts.error().message);
    }
    if (!required_ports) {
        return core::Result<AssemblyDefinition>::failure(required_ports.error().code,
                                                         required_ports.error().message);
    }
    if (!capacity || !requires_heat || !requires_power) {
        return core::Result<AssemblyDefinition>::failure(
            "assembly_prototype.invalid_runtime_fields",
            "assembly capacity or runtime requirements are invalid");
    }

    AssemblyDefinition definition;
    definition.prototype_id = prototype.id;
    definition.part_requirements = std::move(required_parts).value();
    for (auto& part : optional_parts.value()) {
        definition.part_requirements.push_back(std::move(part));
    }
    definition.required_ports = std::move(required_ports).value();
    auto stages = parse_string_list(prototype, "construction_stages");
    if (!stages.empty())
        definition.construction_stages = std::move(stages);
    definition.capabilities = parse_string_list(prototype, "capabilities");
    definition.validation_rule_ids = parse_string_list(prototype, "validation_rules");
    definition.room_requirements = parse_string_list(prototype, "room_requirements");
    definition.capacity = capacity.value();
    definition.requires_heat = requires_heat.value();
    definition.requires_power = requires_power.value();
    if (const auto* panel = field(prototype, "ui_panel"))
        definition.ui_panel = *panel;
    for (const auto& process : parse_string_list(prototype, "allowed_processes")) {
        auto parsed = core::PrototypeId::parse(process);
        if (!parsed) {
            return core::Result<AssemblyDefinition>::failure("assembly_prototype.invalid_process",
                                                             "allowed process id is invalid");
        }
        definition.allowed_processes.push_back(std::move(parsed).value());
    }
    auto layout_status = apply_part_layouts(prototype, definition.part_requirements);
    if (!layout_status) {
        return core::Result<AssemblyDefinition>::failure(layout_status.error().code,
                                                         layout_status.error().message);
    }
    layout_status = apply_port_layouts(prototype, definition.required_ports);
    if (!layout_status)
        return core::Result<AssemblyDefinition>::failure(layout_status.error().code,
                                                         layout_status.error().message);

    auto status = definition.validate();
    if (!status) {
        return core::Result<AssemblyDefinition>::failure(status.error().code,
                                                         status.error().message);
    }
    return core::Result<AssemblyDefinition>::success(std::move(definition));
}

} // namespace heartstead::assemblies
