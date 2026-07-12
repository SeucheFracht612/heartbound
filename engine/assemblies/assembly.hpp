#pragma once

#include "engine/core/ids.hpp"
#include "engine/core/result.hpp"
#include "engine/networks/spatial_network.hpp"
#include "engine/world/coords/world_coords.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace heartstead::assemblies {

struct AssemblyPartRequirement {
    std::string name;
    core::PrototypeId prototype_id;
    bool optional = false;
    world::BlockCoord relative_coord;
    std::uint32_t construction_stage = 0;
    std::string role;

    AssemblyPartRequirement() = default;
    AssemblyPartRequirement(std::string part_name, core::PrototypeId prototype, bool is_optional)
        : name(std::move(part_name)), prototype_id(std::move(prototype)), optional(is_optional) {}
};

struct AssemblyPort {
    std::string name;
    networks::NetworkKind kind = networks::NetworkKind::logistics;
    core::SaveId source_build_piece_id;
    std::uint32_t capacity = 1;
    world::BlockCoord relative_coord;

    AssemblyPort() = default;
    AssemblyPort(std::string port_name, networks::NetworkKind network_kind, core::SaveId source_id,
                 std::uint32_t port_capacity, world::BlockCoord relative = {})
        : name(std::move(port_name)), kind(network_kind), source_build_piece_id(source_id),
          capacity(port_capacity), relative_coord(relative) {}
};

struct AssemblyDefinition {
    core::PrototypeId prototype_id;
    std::vector<AssemblyPartRequirement> part_requirements;
    std::vector<AssemblyPort> required_ports;
    std::vector<std::string> construction_stages{"construction"};
    std::vector<std::string> capabilities;
    std::vector<core::PrototypeId> allowed_processes;
    std::vector<std::string> validation_rule_ids;
    std::vector<std::string> room_requirements;
    std::string ui_panel;
    std::uint32_t capacity = 1;
    bool requires_heat = false;
    bool requires_power = false;

    [[nodiscard]] core::Status validate() const;
};

struct AssemblyPart {
    std::string name;
    core::SaveId build_piece_id;
    core::PrototypeId prototype_id;
    world::BlockCoord relative_coord;

    AssemblyPart() = default;
    AssemblyPart(std::string part_name, core::SaveId piece_id, core::PrototypeId prototype,
                 world::BlockCoord relative = {})
        : name(std::move(part_name)), build_piece_id(piece_id), prototype_id(std::move(prototype)),
          relative_coord(relative) {}
};

enum class AssemblyState : std::uint8_t {
    blueprint,
    constructing,
    drying,
    maiden_firing,
    ready,
    operating,
    failed,
};

[[nodiscard]] std::string_view assembly_state_name(AssemblyState state) noexcept;
[[nodiscard]] core::Result<AssemblyState> parse_assembly_state(std::string_view value);

struct AssemblyRecord {
    core::SaveId assembly_id;
    core::SaveId root_build_piece_id;
    core::PrototypeId prototype_id;
    std::vector<AssemblyPart> parts;
    std::vector<AssemblyPort> ports;
    bool operating = false;
    AssemblyState state = AssemblyState::ready;
    std::uint32_t current_stage = 0;
    std::uint64_t revision = 1;
    std::vector<std::string> capabilities;
    std::vector<core::ProcessId> process_slots;
    std::string failure_reason;
    std::string custom_state;
    world::BlockCoord root_coord;

    [[nodiscard]] core::Status validate_identity() const;
    [[nodiscard]] core::Status validate_record() const;
};

struct AssemblyValidation {
    bool valid = false;
    std::vector<std::string> missing_required_parts;
    std::vector<std::string> mismatched_parts;
    std::vector<std::string> duplicate_parts;
    std::vector<std::string> missing_ports;
};

class AssemblyValidator {
  public:
    [[nodiscard]] static AssemblyValidation validate(const AssemblyDefinition& definition,
                                                     const AssemblyRecord& record);
};

class AssemblyRuntime {
  public:
    [[nodiscard]] static core::Result<AssemblyRecord>
    create_blueprint(core::SaveId assembly_id, core::SaveId root_build_piece_id,
                     const AssemblyDefinition& definition);
    [[nodiscard]] static core::Status
    place_part(AssemblyRecord& record, const AssemblyDefinition& definition, AssemblyPart part);
    [[nodiscard]] static core::Status advance_stage(AssemblyRecord& record,
                                                    const AssemblyDefinition& definition);
    [[nodiscard]] static core::Status transition(AssemblyRecord& record, AssemblyState target,
                                                 std::string reason = {});
    [[nodiscard]] static core::Status attach_process(AssemblyRecord& record,
                                                     core::ProcessId process_id);
    [[nodiscard]] static core::Status detach_process(AssemblyRecord& record,
                                                     core::ProcessId process_id);
};

} // namespace heartstead::assemblies
