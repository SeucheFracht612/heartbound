#include "engine/assemblies/assembly.hpp"

#include <algorithm>
#include <map>
#include <ranges>
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

std::string_view assembly_state_name(AssemblyState state) noexcept {
    switch (state) {
    case AssemblyState::blueprint:
        return "blueprint";
    case AssemblyState::constructing:
        return "constructing";
    case AssemblyState::drying:
        return "drying";
    case AssemblyState::maiden_firing:
        return "maiden_firing";
    case AssemblyState::ready:
        return "ready";
    case AssemblyState::operating:
        return "operating";
    case AssemblyState::failed:
        return "failed";
    }
    return "failed";
}

core::Result<AssemblyState> parse_assembly_state(std::string_view value) {
    if (value == "blueprint")
        return core::Result<AssemblyState>::success(AssemblyState::blueprint);
    if (value == "constructing")
        return core::Result<AssemblyState>::success(AssemblyState::constructing);
    if (value == "drying")
        return core::Result<AssemblyState>::success(AssemblyState::drying);
    if (value == "maiden_firing")
        return core::Result<AssemblyState>::success(AssemblyState::maiden_firing);
    if (value == "ready")
        return core::Result<AssemblyState>::success(AssemblyState::ready);
    if (value == "operating")
        return core::Result<AssemblyState>::success(AssemblyState::operating);
    if (value == "failed")
        return core::Result<AssemblyState>::success(AssemblyState::failed);
    return core::Result<AssemblyState>::failure("assembly.invalid_state",
                                                "assembly state name is unknown");
}

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
        if (requirement.construction_stage >= construction_stages.size()) {
            return core::Status::failure("assembly_definition.invalid_part_stage",
                                         "assembly part references an unknown construction stage");
        }
        if (!requirement.role.empty() && !core::is_valid_local_id(requirement.role)) {
            return core::Status::failure("assembly_definition.invalid_part_role",
                                         "assembly part role must be a valid local id");
        }
    }

    if (construction_stages.empty()) {
        return core::Status::failure("assembly_definition.missing_stages",
                                     "assembly definition requires a construction stage");
    }
    std::set<std::string> stage_names;
    for (const auto& stage : construction_stages) {
        if (!core::is_valid_local_id(stage) || !stage_names.insert(stage).second) {
            return core::Status::failure("assembly_definition.invalid_stage",
                                         "assembly construction stages must be unique local ids");
        }
    }
    if (capacity == 0) {
        return core::Status::failure("assembly_definition.invalid_capacity",
                                     "assembly capacity must be non-zero");
    }
    std::set<std::string> capability_names;
    for (const auto& capability : capabilities) {
        if (!core::is_valid_local_id(capability) || !capability_names.insert(capability).second) {
            return core::Status::failure("assembly_definition.invalid_capability",
                                         "assembly capabilities must be unique local ids");
        }
    }
    std::set<std::string> process_ids;
    for (const auto& process : allowed_processes) {
        if (!process.is_valid() || !process_ids.insert(process.value()).second) {
            return core::Status::failure("assembly_definition.invalid_process",
                                         "assembly allowed processes must be unique valid ids");
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

    if (revision == 0) {
        return core::Status::failure("assembly.invalid_revision",
                                     "assembly revision must be non-zero");
    }
    std::set<std::uint64_t> process_ids;
    for (const auto process_id : process_slots) {
        if (!process_id.is_valid() || !process_ids.insert(process_id.value()).second) {
            return core::Status::failure("assembly.invalid_process_slot",
                                         "assembly process slots require unique valid ids");
        }
    }

    return core::Status::ok();
}

core::Result<AssemblyRecord>
AssemblyRuntime::create_blueprint(core::SaveId assembly_id, core::SaveId root_build_piece_id,
                                  const AssemblyDefinition& definition) {
    auto status = definition.validate();
    if (!status)
        return core::Result<AssemblyRecord>::failure(status.error().code, status.error().message);
    AssemblyRecord record;
    record.assembly_id = assembly_id;
    record.root_build_piece_id = root_build_piece_id;
    record.prototype_id = definition.prototype_id;
    record.state = AssemblyState::blueprint;
    record.operating = false;
    record.capabilities = definition.capabilities;
    status = record.validate_record();
    if (!status)
        return core::Result<AssemblyRecord>::failure(status.error().code, status.error().message);
    return core::Result<AssemblyRecord>::success(std::move(record));
}

core::Status AssemblyRuntime::place_part(AssemblyRecord& record,
                                         const AssemblyDefinition& definition, AssemblyPart part) {
    if (record.state != AssemblyState::blueprint && record.state != AssemblyState::constructing) {
        return core::Status::failure("assembly.part_placement_invalid_state",
                                     "assembly parts can only be placed into a blueprint");
    }
    const auto requirement =
        std::ranges::find_if(definition.part_requirements, [&part](const auto& candidate) {
            return candidate.name == part.name;
        });
    if (requirement == definition.part_requirements.end() ||
        requirement->prototype_id != part.prototype_id) {
        return core::Status::failure("assembly.part_requirement_mismatch",
                                     "assembly part does not match the ghost blueprint slot");
    }
    if (requirement->construction_stage != record.current_stage) {
        return core::Status::failure("assembly.part_wrong_stage",
                                     "assembly part belongs to a different construction stage");
    }
    if (std::ranges::any_of(record.parts,
                            [&part](const auto& existing) { return existing.name == part.name; })) {
        return core::Status::failure("assembly.duplicate_part",
                                     "assembly part slot is already filled");
    }
    if (!part.build_piece_id.is_valid()) {
        return core::Status::failure("assembly.invalid_part_build_piece",
                                     "placed assembly part requires a stable build piece id");
    }
    record.parts.push_back(std::move(part));
    record.state = AssemblyState::constructing;
    ++record.revision;
    return core::Status::ok();
}

core::Status AssemblyRuntime::advance_stage(AssemblyRecord& record,
                                            const AssemblyDefinition& definition) {
    if (record.current_stage >= definition.construction_stages.size()) {
        return core::Status::failure("assembly.stage_out_of_range",
                                     "assembly construction stage is out of range");
    }
    for (const auto& requirement : definition.part_requirements) {
        if (requirement.optional || requirement.construction_stage != record.current_stage)
            continue;
        if (!std::ranges::any_of(record.parts, [&requirement](const auto& part) {
                return part.name == requirement.name &&
                       part.prototype_id == requirement.prototype_id;
            })) {
            return core::Status::failure("assembly.stage_incomplete",
                                         "required ghost blueprint parts are still missing");
        }
    }
    ++record.current_stage;
    if (record.current_stage >= definition.construction_stages.size()) {
        record.state = AssemblyState::ready;
    } else {
        record.state = AssemblyState::constructing;
    }
    record.operating = false;
    ++record.revision;
    return core::Status::ok();
}

core::Status AssemblyRuntime::transition(AssemblyRecord& record, AssemblyState target,
                                         std::string reason) {
    const auto source = record.state;
    const bool allowed =
        target == AssemblyState::failed ||
        (source == AssemblyState::ready && target == AssemblyState::operating) ||
        (source == AssemblyState::operating && target == AssemblyState::ready) ||
        (source == AssemblyState::ready && target == AssemblyState::drying) ||
        (source == AssemblyState::drying && target == AssemblyState::maiden_firing) ||
        (source == AssemblyState::maiden_firing && target == AssemblyState::ready);
    if (!allowed) {
        return core::Status::failure("assembly.invalid_transition",
                                     "assembly state transition is not allowed");
    }
    record.state = target;
    record.operating = target == AssemblyState::operating;
    record.failure_reason = target == AssemblyState::failed ? std::move(reason) : std::string{};
    ++record.revision;
    return core::Status::ok();
}

core::Status AssemblyRuntime::attach_process(AssemblyRecord& record, core::ProcessId process_id) {
    if (!process_id.is_valid()) {
        return core::Status::failure("assembly.invalid_process_slot",
                                     "assembly process slot id must be valid");
    }
    if (std::ranges::find(record.process_slots, process_id) != record.process_slots.end()) {
        return core::Status::failure("assembly.duplicate_process_slot",
                                     "process is already attached to the assembly");
    }
    record.process_slots.push_back(process_id);
    ++record.revision;
    return core::Status::ok();
}

core::Status AssemblyRuntime::detach_process(AssemblyRecord& record, core::ProcessId process_id) {
    const auto found = std::ranges::find(record.process_slots, process_id);
    if (found == record.process_slots.end()) {
        return core::Status::failure("assembly.missing_process_slot",
                                     "process is not attached to the assembly");
    }
    record.process_slots.erase(found);
    ++record.revision;
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
