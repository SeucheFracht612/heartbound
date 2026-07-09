#include "engine/assemblies/assembly.hpp"

#include <map>
#include <set>

namespace heartstead::assemblies {

namespace {

[[nodiscard]] bool is_known_network_kind(networks::NetworkKind kind) noexcept {
    switch (kind) {
    case networks::NetworkKind::road:
    case networks::NetworkKind::cart_access:
    case networks::NetworkKind::storage_access:
    case networks::NetworkKind::power:
    case networks::NetworkKind::ward:
    case networks::NetworkKind::smoke_ventilation:
    case networks::NetworkKind::water:
    case networks::NetworkKind::logistics:
        return true;
    }
    return false;
}

[[nodiscard]] bool has_port(const AssemblyRecord& record, const AssemblyPort& port) {
    for (const auto& existing : record.ports) {
        if (existing.name == port.name && existing.kind == port.kind) {
            return true;
        }
    }
    return false;
}

} // namespace

core::Status AssemblyDefinition::validate() const {
    if (!prototype_id.is_valid()) {
        return core::Status::failure("assembly_definition.invalid_prototype",
                                     "assembly definition prototype id must be valid");
    }

    std::set<std::string> requirement_names;
    for (const auto& requirement : part_requirements) {
        if (requirement.name.empty()) {
            return core::Status::failure("assembly_definition.invalid_part_name",
                                         "assembly part requirement name is required");
        }
        if (!requirement.prototype_id.is_valid()) {
            return core::Status::failure("assembly_definition.invalid_part_prototype",
                                         "assembly part requirement prototype id must be valid");
        }
        if (!requirement_names.insert(requirement.name).second) {
            return core::Status::failure("assembly_definition.duplicate_part",
                                         "duplicate assembly part requirement: " +
                                             requirement.name);
        }
    }

    std::set<std::string> port_names;
    for (const auto& port : required_ports) {
        if (port.name.empty()) {
            return core::Status::failure("assembly_definition.invalid_port_name",
                                         "assembly port name is required");
        }
        if (!core::is_valid_local_id(port.name)) {
            return core::Status::failure("assembly_definition.invalid_port_name",
                                         "assembly port name must be a valid local id: " +
                                             port.name);
        }
        if (!is_known_network_kind(port.kind)) {
            return core::Status::failure("assembly_definition.invalid_port_kind",
                                         "assembly port kind is unknown");
        }
        if (port.capacity == 0) {
            return core::Status::failure("assembly_definition.invalid_port_capacity",
                                         "assembly port capacity must be non-zero");
        }
        if (!port_names.insert(port.name).second) {
            return core::Status::failure("assembly_definition.duplicate_port",
                                         "duplicate assembly port: " + port.name);
        }
    }

    return core::Status::ok();
}

core::Status AssemblyRecord::validate_identity() const {
    if (!assembly_id.is_valid()) {
        return core::Status::failure("assembly.invalid_id", "assembly needs a stable save id");
    }
    if (!root_build_piece_id.is_valid()) {
        return core::Status::failure("assembly.invalid_root",
                                     "assembly needs a root build piece save id");
    }
    if (!prototype_id.is_valid()) {
        return core::Status::failure("assembly.invalid_prototype",
                                     "assembly prototype id must be valid");
    }
    return core::Status::ok();
}

core::Status AssemblyRecord::validate_record() const {
    auto status = validate_identity();
    if (!status) {
        return status;
    }

    std::set<std::string> part_names;
    for (const auto& part : parts) {
        if (part.name.empty()) {
            return core::Status::failure("assembly.invalid_part_name",
                                         "assembly part name is required");
        }
        if (!part.build_piece_id.is_valid()) {
            return core::Status::failure("assembly.invalid_part_build_piece",
                                         "assembly part build piece id is invalid");
        }
        if (!part.prototype_id.is_valid()) {
            return core::Status::failure("assembly.invalid_part_prototype",
                                         "assembly part prototype id is invalid");
        }
        if (!part_names.insert(part.name).second) {
            return core::Status::failure("assembly.duplicate_part",
                                         "duplicate assembly part name: " + part.name);
        }
    }

    std::set<std::string> port_names;
    for (const auto& port : ports) {
        if (port.name.empty()) {
            return core::Status::failure("assembly.invalid_port_name",
                                         "assembly port name is required");
        }
        if (!core::is_valid_local_id(port.name)) {
            return core::Status::failure("assembly.invalid_port_name",
                                         "assembly port name must be a valid local id: " +
                                             port.name);
        }
        if (!is_known_network_kind(port.kind)) {
            return core::Status::failure("assembly.invalid_port_kind",
                                         "assembly port kind is unknown");
        }
        if (!port.source_build_piece_id.is_valid()) {
            return core::Status::failure("assembly.invalid_port_source",
                                         "assembly port source build piece id is invalid");
        }
        if (port.capacity == 0) {
            return core::Status::failure("assembly.invalid_port_capacity",
                                         "assembly port capacity must be non-zero");
        }
        if (!port_names.insert(port.name).second) {
            return core::Status::failure("assembly.duplicate_port",
                                         "duplicate assembly port name: " + port.name);
        }
    }

    return core::Status::ok();
}

AssemblyValidation AssemblyValidator::validate(const AssemblyDefinition& definition,
                                               const AssemblyRecord& record) {
    AssemblyValidation result;

    if (!definition.validate()) {
        result.valid = false;
        return result;
    }
    if (!record.validate_record() || record.prototype_id != definition.prototype_id) {
        result.valid = false;
        result.mismatched_parts.push_back("assembly record does not satisfy definition");
        return result;
    }

    std::map<std::string, AssemblyPart> parts_by_name;
    for (const auto& part : record.parts) {
        if (part.name.empty() || !part.build_piece_id.is_valid() || !part.prototype_id.is_valid()) {
            result.mismatched_parts.push_back(part.name.empty() ? "<unnamed>" : part.name);
            continue;
        }
        if (!parts_by_name.emplace(part.name, part).second) {
            result.duplicate_parts.push_back(part.name);
        }
    }

    for (const auto& requirement : definition.part_requirements) {
        const auto found = parts_by_name.find(requirement.name);
        if (found == parts_by_name.end()) {
            if (!requirement.optional) {
                result.missing_required_parts.push_back(requirement.name);
            }
            continue;
        }
        if (found->second.prototype_id != requirement.prototype_id) {
            result.mismatched_parts.push_back(requirement.name);
        }
    }

    for (const auto& port : definition.required_ports) {
        if (!has_port(record, port)) {
            result.missing_ports.push_back(port.name);
        }
    }

    result.valid = result.missing_required_parts.empty() && result.mismatched_parts.empty() &&
                   result.duplicate_parts.empty() && result.missing_ports.empty();
    return result;
}

} // namespace heartstead::assemblies
