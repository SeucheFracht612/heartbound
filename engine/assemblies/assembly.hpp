#pragma once

#include "engine/core/ids.hpp"
#include "engine/core/result.hpp"
#include "engine/networks/spatial_network.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace heartstead::assemblies {

struct AssemblyPartRequirement {
    std::string name;
    core::PrototypeId prototype_id;
    bool optional = false;
};

struct AssemblyPort {
    std::string name;
    networks::NetworkKind kind = networks::NetworkKind::logistics;
    core::SaveId source_build_piece_id;
    std::uint32_t capacity = 1;
};

struct AssemblyDefinition {
    core::PrototypeId prototype_id;
    std::vector<AssemblyPartRequirement> part_requirements;
    std::vector<AssemblyPort> required_ports;

    [[nodiscard]] core::Status validate() const;
};

struct AssemblyPart {
    std::string name;
    core::SaveId build_piece_id;
    core::PrototypeId prototype_id;
};

struct AssemblyRecord {
    core::SaveId assembly_id;
    core::SaveId root_build_piece_id;
    core::PrototypeId prototype_id;
    std::vector<AssemblyPart> parts;
    std::vector<AssemblyPort> ports;
    bool operating = false;

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

} // namespace heartstead::assemblies
