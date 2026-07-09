#include "engine/build/build_piece.hpp"

#include <set>
#include <string_view>

namespace heartstead::build {

namespace {

[[nodiscard]] bool is_known_construction_state(ConstructionState state) noexcept {
    switch (state) {
    case ConstructionState::planned:
    case ConstructionState::under_construction:
    case ConstructionState::complete:
    case ConstructionState::damaged:
        return true;
    }
    return false;
}

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

[[nodiscard]] core::Status validate_tag_list(const std::vector<std::string>& tags,
                                             std::string_view error_code, std::string_view label) {
    std::set<std::string> seen;
    for (const auto& tag : tags) {
        if (!core::is_valid_local_id(tag)) {
            return core::Status::failure(std::string(error_code),
                                         std::string(label) + " must be a valid local id: " + tag);
        }
        if (!seen.insert(tag).second) {
            return core::Status::failure(std::string(error_code),
                                         "duplicate " + std::string(label) + ": " + tag);
        }
    }

    return core::Status::ok();
}

} // namespace

core::Status BuildPieceRecord::validate() const {
    if (!object_id.is_valid()) {
        return core::Status::failure("build_piece.invalid_id",
                                     "build piece needs a stable save id");
    }
    if (!prototype_id.is_valid()) {
        return core::Status::failure("build_piece.invalid_prototype",
                                     "build piece prototype id must be valid");
    }
    if (transform.scale.x <= 0.0 || transform.scale.y <= 0.0 || transform.scale.z <= 0.0) {
        return core::Status::failure("build_piece.invalid_scale",
                                     "build piece scale must be positive");
    }
    if (!transform.is_finite()) {
        return core::Status::failure("build_piece.invalid_transform",
                                     "build piece transform must be finite");
    }
    if (!is_known_construction_state(construction_state)) {
        return core::Status::failure("build_piece.invalid_construction_state",
                                     "build piece construction state is unknown");
    }

    std::set<std::string> socket_names;
    for (const auto& socket : sockets) {
        if (socket.name.empty()) {
            return core::Status::failure("build_piece.invalid_socket", "socket name is required");
        }
        if (!core::is_valid_local_id(socket.name)) {
            return core::Status::failure("build_piece.invalid_socket_name",
                                         "socket name must be a valid local id: " + socket.name);
        }
        if (!socket.local_position.is_finite()) {
            return core::Status::failure("build_piece.invalid_socket_position",
                                         "socket local position must be finite");
        }
        if (!socket.tag.empty() && !core::is_valid_local_id(socket.tag)) {
            return core::Status::failure("build_piece.invalid_socket_tag",
                                         "socket tag must be a valid local id: " + socket.tag);
        }
        if (!socket_names.insert(socket.name).second) {
            return core::Status::failure("build_piece.invalid_socket",
                                         "duplicate name: " + socket.name);
        }
    }

    std::set<std::string> port_names;
    for (const auto& port : network_ports) {
        if (port.name.empty()) {
            return core::Status::failure("build_piece.invalid_port", "port name is required");
        }
        if (!core::is_valid_local_id(port.name)) {
            return core::Status::failure("build_piece.invalid_port_name",
                                         "port name must be a valid local id: " + port.name);
        }
        if (!is_known_network_kind(port.kind)) {
            return core::Status::failure("build_piece.invalid_port_kind",
                                         "network port kind is unknown");
        }
        if (port.capacity == 0) {
            return core::Status::failure("build_piece.invalid_port_capacity",
                                         "network port capacity must be non-zero");
        }
        if (!port_names.insert(port.name).second) {
            return core::Status::failure("build_piece.invalid_port",
                                         "duplicate name: " + port.name);
        }
    }

    auto material_status =
        validate_tag_list(material_tags, "build_piece.invalid_material_tag", "material tag");
    if (!material_status) {
        return material_status;
    }

    return validate_tag_list(room_contribution_tags, "build_piece.invalid_room_contribution_tag",
                             "room contribution tag");
}

bool BuildPieceRecord::contributes_to_rooms() const noexcept {
    return !room_contribution_tags.empty();
}

bool BuildPieceRecord::exposes_network_ports() const noexcept {
    return !network_ports.empty();
}

} // namespace heartstead::build
