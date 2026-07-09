#pragma once

#include "engine/modding/generic_prototype.hpp"
#include "engine/rooms/room_graph.hpp"

#include <string>
#include <vector>

namespace heartstead::rooms {

struct RoomDescriptorDefinition {
    core::PrototypeId prototype_id;
    std::string code;
    std::string label;
    RoomDescriptorSeverity severity = RoomDescriptorSeverity::neutral;
    std::vector<std::string> tags;

    [[nodiscard]] core::Status validate() const;
    [[nodiscard]] RoomDescriptor descriptor() const;
};

[[nodiscard]] core::Result<RoomDescriptorDefinition>
room_descriptor_definition_from_prototype(const modding::GenericPrototype& prototype);

} // namespace heartstead::rooms
