#pragma once

#include "engine/core/ids.hpp"
#include "engine/core/result.hpp"
#include "engine/dirty/dirty_region.hpp"
#include "engine/world/coords/world_coords.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace heartstead::networks {

struct NetworkNodeTag;
struct NetworkEdgeTag;
struct NetworkPortTag;

using NetworkNodeId = core::StrongU64Id<NetworkNodeTag>;
using NetworkEdgeId = core::StrongU64Id<NetworkEdgeTag>;
using NetworkPortId = core::StrongU64Id<NetworkPortTag>;

enum class NetworkKind {
    road,
    cart_access,
    storage_access,
    power,
    ward,
    smoke_ventilation,
    water,
    logistics,
};

using NetworkCoord = world::BlockCoord;

struct NetworkNode {
    NetworkNodeId id;
    NetworkCoord coord;
    std::uint32_t capacity = 1;
    std::string label;
};

struct NetworkEdge {
    NetworkEdgeId id;
    NetworkNodeId a;
    NetworkNodeId b;
    std::uint32_t quality = 100;
    std::uint32_t capacity = 1;
    bool blocked = false;
};

struct NetworkPort {
    NetworkPortId id;
    NetworkNodeId node_id;
    std::string name;
    std::uint32_t capacity = 1;
    core::SaveId owner_id;
    core::SaveId source_build_piece_id;
};

struct LogisticsRouteEffects {
    bool reachable = false;
    std::uint32_t edge_count = 0;
    std::uint32_t cart_speed_per_mille = 0;
    std::uint32_t animal_stamina_cost_per_mille = 1000;
    std::uint32_t pathfinding_reliability_per_mille = 0;
    std::uint32_t travel_safety_per_mille = 0;
    std::uint32_t corpse_recovery_per_mille = 0;
    std::uint32_t weather_resistance_per_mille = 0;
    std::uint32_t bottleneck_capacity = 0;
};

class SpatialNetwork {
  public:
    explicit SpatialNetwork(NetworkKind kind);

    [[nodiscard]] NetworkKind kind() const noexcept;
    [[nodiscard]] core::Status add_node(NetworkNode node);
    [[nodiscard]] core::Status add_edge(NetworkEdge edge);
    [[nodiscard]] core::Status add_port(NetworkPort port);

    [[nodiscard]] const NetworkNode* find_node(NetworkNodeId id) const noexcept;
    [[nodiscard]] const NetworkEdge* find_edge(NetworkEdgeId id) const noexcept;
    [[nodiscard]] const NetworkPort* find_port(NetworkPortId id) const noexcept;
    [[nodiscard]] std::vector<const NetworkNode*> nodes() const;
    [[nodiscard]] std::vector<NetworkNodeId> neighbors(NetworkNodeId node_id) const;
    [[nodiscard]] bool can_reach(NetworkNodeId start, NetworkNodeId goal) const;
    [[nodiscard]] LogisticsRouteEffects route_effects(NetworkNodeId start,
                                                      NetworkNodeId goal) const;
    [[nodiscard]] std::size_t node_count() const noexcept;
    [[nodiscard]] std::size_t edge_count() const noexcept;
    [[nodiscard]] std::size_t blocked_edge_count() const noexcept;
    [[nodiscard]] std::size_t port_count() const noexcept;
    [[nodiscard]] std::size_t owned_port_count() const noexcept;
    [[nodiscard]] std::size_t sourced_port_count() const noexcept;
    [[nodiscard]] std::size_t port_count_for_owner(core::SaveId owner_id) const noexcept;
    [[nodiscard]] std::uint64_t total_node_capacity() const noexcept;
    [[nodiscard]] std::uint64_t total_edge_capacity() const noexcept;
    [[nodiscard]] std::uint64_t total_port_capacity() const noexcept;
    [[nodiscard]] std::uint64_t total_port_capacity_for_owner(core::SaveId owner_id) const noexcept;

    void mark_dirty() noexcept;
    [[nodiscard]] core::Status mark_dirty_region(dirty::DirtyRegionTracker& dirty_regions,
                                                 dirty::DirtyRegionBounds bounds,
                                                 std::string reason);
    void clear_dirty() noexcept;
    [[nodiscard]] bool is_dirty() const noexcept;

  private:
    NetworkKind kind_;
    std::unordered_map<std::uint64_t, NetworkNode> nodes_;
    std::unordered_map<std::uint64_t, NetworkEdge> edges_;
    std::unordered_map<std::uint64_t, NetworkPort> ports_;
    bool dirty_ = false;
};

[[nodiscard]] dirty::DirtyRegionKind dirty_region_kind_for(NetworkKind kind) noexcept;
[[nodiscard]] std::string_view network_kind_name(NetworkKind kind) noexcept;
[[nodiscard]] NetworkKind network_kind_for_port_name(std::string_view port_name) noexcept;

} // namespace heartstead::networks
