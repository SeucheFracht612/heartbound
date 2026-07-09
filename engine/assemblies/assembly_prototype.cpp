#include "engine/assemblies/assembly_prototype.hpp"

#include "engine/modding/prototype_registry.hpp"

#include <string>
#include <string_view>
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
            {std::string(port_name), networks::network_kind_for_port_name(port_name), {}, 1});
    }

    return core::Result<std::vector<AssemblyPort>>::success(std::move(ports));
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

    AssemblyDefinition definition;
    definition.prototype_id = prototype.id;
    definition.part_requirements = std::move(required_parts).value();
    for (auto& part : optional_parts.value()) {
        definition.part_requirements.push_back(std::move(part));
    }
    definition.required_ports = std::move(required_ports).value();

    auto status = definition.validate();
    if (!status) {
        return core::Result<AssemblyDefinition>::failure(status.error().code,
                                                         status.error().message);
    }
    return core::Result<AssemblyDefinition>::success(std::move(definition));
}

} // namespace heartstead::assemblies
