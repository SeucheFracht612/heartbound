#include "engine/networks/network_derivation.hpp"

#include "engine/core/hash.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace heartstead::networks {

namespace {

enum class DerivedNodeSource : std::uint8_t {
    build_piece_port = 1,
    assembly_port = 2,
};

struct DerivedNodeRef {
    NetworkKind kind = NetworkKind::logistics;
    NetworkNodeId node_id;
    NetworkCoord coord;
    std::uint32_t capacity = 1;
    core::SaveId owner_id;
    core::SaveId source_build_piece_id;
};

[[nodiscard]] std::uint64_t key(NetworkKind kind) noexcept {
    return static_cast<std::uint64_t>(kind);
}

[[nodiscard]] bool same_coord(NetworkCoord lhs, NetworkCoord rhs) noexcept {
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
}

[[nodiscard]] NetworkCoord coord_from_transform(const build::Transform& transform) noexcept {
    return {transform.position.anchor.x, transform.position.anchor.y,
            transform.position.anchor.z};
}

[[nodiscard]] std::uint64_t make_stable_hash(std::string_view domain, DerivedNodeSource source,
                                             core::SaveId owner_id,
                                             core::SaveId source_build_piece_id, NetworkKind kind,
                                             std::size_t port_index,
                                             std::string_view port_name) noexcept {
    core::StableHash64 hasher;
    hasher.add_string(domain);
    hasher.add_byte(static_cast<std::uint8_t>(source));
    hasher.add_u64_le(owner_id.value());
    hasher.add_u64_le(source_build_piece_id.value());
    hasher.add_u64_le(static_cast<std::uint64_t>(kind));
    hasher.add_u64_le(static_cast<std::uint64_t>(port_index));
    hasher.add_string(port_name);
    return hasher.nonzero_value();
}

[[nodiscard]] NetworkNodeId make_node_id(DerivedNodeSource source, core::SaveId owner_id,
                                         core::SaveId source_build_piece_id, NetworkKind kind,
                                         std::size_t port_index,
                                         std::string_view port_name) noexcept {
    return NetworkNodeId::from_value(make_stable_hash(
        "node", source, owner_id, source_build_piece_id, kind, port_index, port_name));
}

[[nodiscard]] NetworkPortId make_port_id(DerivedNodeSource source, core::SaveId owner_id,
                                         core::SaveId source_build_piece_id, NetworkKind kind,
                                         std::size_t port_index,
                                         std::string_view port_name) noexcept {
    return NetworkPortId::from_value(make_stable_hash(
        "port", source, owner_id, source_build_piece_id, kind, port_index, port_name));
}

[[nodiscard]] NetworkEdgeId make_edge_id(NetworkKind kind, NetworkNodeId lhs,
                                         NetworkNodeId rhs) noexcept {
    auto a = lhs.value();
    auto b = rhs.value();
    if (b < a) {
        std::swap(a, b);
    }

    core::StableHash64 hasher;
    hasher.add_string("edge");
    hasher.add_u64_le(static_cast<std::uint64_t>(kind));
    hasher.add_u64_le(a);
    hasher.add_u64_le(b);
    return NetworkEdgeId::from_value(hasher.nonzero_value());
}

[[nodiscard]] std::string build_piece_node_label(core::SaveId build_piece_id,
                                                 std::string_view port_name) {
    std::ostringstream output;
    output << "build:" << build_piece_id.value() << ':' << port_name;
    return output.str();
}

[[nodiscard]] std::string assembly_node_label(core::SaveId assembly_id,
                                              core::SaveId source_build_piece_id,
                                              std::string_view port_name) {
    std::ostringstream output;
    output << "assembly:" << assembly_id.value() << ':' << port_name
           << ":source:" << source_build_piece_id.value();
    return output.str();
}

[[nodiscard]] const build::BuildPieceRecord*
find_build_piece(const std::map<std::uint64_t, const build::BuildPieceRecord*>& build_pieces,
                 core::SaveId id) noexcept {
    const auto found = build_pieces.find(id.value());
    return found == build_pieces.end() ? nullptr : found->second;
}

[[nodiscard]] core::Status add_node_and_port(SpatialNetwork& network, DerivedNodeRef& ref,
                                             NetworkPortId port_id, std::string port_name,
                                             std::string label) {
    auto status =
        network.add_node(NetworkNode{ref.node_id, ref.coord, ref.capacity, std::move(label)});
    if (!status) {
        return status;
    }
    return network.add_port(NetworkPort{port_id, ref.node_id, std::move(port_name), ref.capacity,
                                        ref.owner_id, ref.source_build_piece_id});
}

[[nodiscard]] SpatialNetwork& get_or_create_network(std::map<std::uint64_t, SpatialNetwork>& map,
                                                    NetworkKind kind) {
    const auto network_key = key(kind);
    const auto found = map.find(network_key);
    if (found != map.end()) {
        return found->second;
    }
    auto [it, _] = map.emplace(network_key, SpatialNetwork(kind));
    return it->second;
}

[[nodiscard]] core::Status add_colocated_edges(std::map<std::uint64_t, SpatialNetwork>& networks,
                                               const std::vector<DerivedNodeRef>& nodes,
                                               SpatialNetworkDerivationStats& stats) {
    for (std::size_t lhs_index = 0; lhs_index < nodes.size(); ++lhs_index) {
        for (std::size_t rhs_index = lhs_index + 1; rhs_index < nodes.size(); ++rhs_index) {
            const auto& lhs = nodes[lhs_index];
            const auto& rhs = nodes[rhs_index];
            if (lhs.kind != rhs.kind || !same_coord(lhs.coord, rhs.coord)) {
                continue;
            }

            auto& network = get_or_create_network(networks, lhs.kind);
            auto status = network.add_edge(NetworkEdge{
                make_edge_id(lhs.kind, lhs.node_id, rhs.node_id),
                lhs.node_id,
                rhs.node_id,
                100,
                std::min(lhs.capacity, rhs.capacity),
                false,
            });
            if (!status) {
                return status;
            }
            ++stats.generated_edge_count;
        }
    }
    return core::Status::ok();
}

} // namespace

core::Result<SpatialNetworkDerivationResult>
SpatialNetworkDeriver::derive(const SpatialNetworkDerivationInput& input) {
    SpatialNetworkDerivationResult result;
    std::map<std::uint64_t, const build::BuildPieceRecord*> build_pieces_by_id;
    std::map<std::uint64_t, SpatialNetwork> networks;
    std::vector<DerivedNodeRef> derived_nodes;

    for (const auto* build_piece : input.build_pieces) {
        if (build_piece == nullptr) {
            return core::Result<SpatialNetworkDerivationResult>::failure(
                "network_derivation.null_build_piece", "network derivation got a null build piece");
        }
        auto status = build_piece->validate();
        if (!status) {
            return core::Result<SpatialNetworkDerivationResult>::failure(status.error().code,
                                                                         status.error().message);
        }
        if (!build_pieces_by_id.emplace(build_piece->object_id.value(), build_piece).second) {
            return core::Result<SpatialNetworkDerivationResult>::failure(
                "network_derivation.duplicate_build_piece",
                "network derivation got duplicate build piece save ids");
        }
    }

    for (const auto* build_piece : input.build_pieces) {
        if (build_piece->construction_state != build::ConstructionState::complete) {
            if (build_piece->exposes_network_ports()) {
                ++result.stats.skipped_incomplete_build_piece_count;
            }
            continue;
        }

        const auto coord = coord_from_transform(build_piece->transform);
        for (std::size_t index = 0; index < build_piece->network_ports.size(); ++index) {
            const auto& port = build_piece->network_ports[index];
            auto& network = get_or_create_network(networks, port.kind);
            DerivedNodeRef ref{
                port.kind,
                make_node_id(DerivedNodeSource::build_piece_port, build_piece->object_id,
                             build_piece->object_id, port.kind, index, port.name),
                coord,
                port.capacity,
                build_piece->object_id,
                build_piece->object_id,
            };

            auto status = add_node_and_port(
                network, ref,
                make_port_id(DerivedNodeSource::build_piece_port, build_piece->object_id,
                             build_piece->object_id, port.kind, index, port.name),
                port.name, build_piece_node_label(build_piece->object_id, port.name));
            if (!status) {
                return core::Result<SpatialNetworkDerivationResult>::failure(
                    status.error().code, status.error().message);
            }
            derived_nodes.push_back(ref);
            ++result.stats.build_piece_port_count;
            ++result.stats.generated_node_count;
            ++result.stats.generated_port_count;
        }
    }

    for (const auto* assembly : input.assemblies) {
        if (assembly == nullptr) {
            return core::Result<SpatialNetworkDerivationResult>::failure(
                "network_derivation.null_assembly", "network derivation got a null assembly");
        }
        auto status = assembly->validate_record();
        if (!status) {
            return core::Result<SpatialNetworkDerivationResult>::failure(status.error().code,
                                                                         status.error().message);
        }

        for (std::size_t index = 0; index < assembly->ports.size(); ++index) {
            const auto& port = assembly->ports[index];
            const auto* source = find_build_piece(build_pieces_by_id, port.source_build_piece_id);
            if (source == nullptr) {
                return core::Result<SpatialNetworkDerivationResult>::failure(
                    "network_derivation.missing_assembly_port_source",
                    "assembly port source build piece is not present");
            }
            if (source->construction_state != build::ConstructionState::complete) {
                return core::Result<SpatialNetworkDerivationResult>::failure(
                    "network_derivation.incomplete_assembly_port_source",
                    "assembly port source build piece must be complete");
            }

            auto& network = get_or_create_network(networks, port.kind);
            DerivedNodeRef ref{
                port.kind,
                make_node_id(DerivedNodeSource::assembly_port, assembly->assembly_id,
                             port.source_build_piece_id, port.kind, index, port.name),
                coord_from_transform(source->transform),
                port.capacity,
                assembly->assembly_id,
                port.source_build_piece_id,
            };

            status = add_node_and_port(
                network, ref,
                make_port_id(DerivedNodeSource::assembly_port, assembly->assembly_id,
                             port.source_build_piece_id, port.kind, index, port.name),
                port.name,
                assembly_node_label(assembly->assembly_id, port.source_build_piece_id, port.name));
            if (!status) {
                return core::Result<SpatialNetworkDerivationResult>::failure(
                    status.error().code, status.error().message);
            }
            derived_nodes.push_back(ref);
            ++result.stats.assembly_port_count;
            ++result.stats.generated_node_count;
            ++result.stats.generated_port_count;
        }
    }

    auto edge_status = add_colocated_edges(networks, derived_nodes, result.stats);
    if (!edge_status) {
        return core::Result<SpatialNetworkDerivationResult>::failure(edge_status.error().code,
                                                                     edge_status.error().message);
    }

    result.stats.network_count = networks.size();
    result.networks.reserve(networks.size());
    for (auto& [_, network] : networks) {
        network.clear_dirty();
        result.networks.push_back(std::move(network));
    }
    return core::Result<SpatialNetworkDerivationResult>::success(std::move(result));
}

} // namespace heartstead::networks
