#pragma once

#include "engine/core/ids.hpp"
#include "engine/core/result.hpp"
#include "engine/math/vector.hpp"
#include "engine/networks/spatial_network.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace heartstead::build {

using Vec3 = math::Vec3d;
using Transform = math::Transform3d;

struct BuildSocket {
    std::string name;
    Vec3 local_position;
    std::string tag;
};

struct BuildNetworkPort {
    std::string name;
    networks::NetworkKind kind = networks::NetworkKind::logistics;
    std::uint32_t capacity = 1;
};

enum class ConstructionState {
    planned,
    under_construction,
    complete,
    damaged,
};

struct BuildPieceRecord {
    core::SaveId object_id;
    core::PrototypeId prototype_id;
    Transform transform;
    ConstructionState construction_state = ConstructionState::planned;
    std::vector<BuildSocket> sockets;
    std::vector<BuildNetworkPort> network_ports;
    std::vector<std::string> material_tags;
    std::vector<std::string> room_contribution_tags;

    [[nodiscard]] core::Status validate() const;
    [[nodiscard]] bool contributes_to_rooms() const noexcept;
    [[nodiscard]] bool exposes_network_ports() const noexcept;
};

} // namespace heartstead::build
