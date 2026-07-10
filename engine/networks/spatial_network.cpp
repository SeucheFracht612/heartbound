#include "engine/networks/spatial_network.hpp"

#include <algorithm>
#include <limits>
#include <queue>
#include <unordered_set>
#include <utility>

namespace heartstead::networks {

SpatialNetwork::SpatialNetwork(NetworkKind kind) : kind_(kind) {}

NetworkKind SpatialNetwork::kind() const noexcept {
    return kind_;
}

core::Status SpatialNetwork::add_node(NetworkNode node) {
    if (!node.id.is_valid()) {
        return core::Status::failure("network.invalid_node_id", "network node id must be valid");
    }
    if (node.capacity == 0) {
        return core::Status::failure("network.invalid_node_capacity",
                                     "network node capacity must be non-zero");
    }

    const auto [_, inserted] = nodes_.emplace(node.id.value(), std::move(node));
    if (!inserted) {
        return core::Status::failure("network.duplicate_node", "network node id already exists");
    }

    mark_dirty();
    return core::Status::ok();
}

core::Status SpatialNetwork::add_edge(NetworkEdge edge) {
    if (!edge.id.is_valid()) {
        return core::Status::failure("network.invalid_edge_id", "network edge id must be valid");
    }
    if (!find_node(edge.a) || !find_node(edge.b)) {
        return core::Status::failure("network.missing_edge_node",
                                     "network edge endpoints must both exist");
    }
    if (edge.a == edge.b) {
        return core::Status::failure("network.self_edge", "network edge endpoints must differ");
    }
    if (edge.capacity == 0) {
        return core::Status::failure("network.invalid_edge_capacity",
                                     "network edge capacity must be non-zero");
    }
    if (edge.quality == 0 || edge.quality > 1000) {
        return core::Status::failure("network.invalid_edge_quality",
                                     "network edge quality must be 1..1000");
    }

    const auto [_, inserted] = edges_.emplace(edge.id.value(), std::move(edge));
    if (!inserted) {
        return core::Status::failure("network.duplicate_edge", "network edge id already exists");
    }

    mark_dirty();
    return core::Status::ok();
}

core::Status SpatialNetwork::add_port(NetworkPort port) {
    if (!port.id.is_valid()) {
        return core::Status::failure("network.invalid_port_id", "network port id must be valid");
    }
    if (!find_node(port.node_id)) {
        return core::Status::failure("network.missing_port_node",
                                     "network port must attach to an existing node");
    }
    if (port.name.empty()) {
        return core::Status::failure("network.invalid_port_name", "network port name is required");
    }
    if (port.capacity == 0) {
        return core::Status::failure("network.invalid_port_capacity",
                                     "network port capacity must be non-zero");
    }

    const auto [_, inserted] = ports_.emplace(port.id.value(), std::move(port));
    if (!inserted) {
        return core::Status::failure("network.duplicate_port", "network port id already exists");
    }

    mark_dirty();
    return core::Status::ok();
}

const NetworkNode* SpatialNetwork::find_node(NetworkNodeId id) const noexcept {
    const auto found = nodes_.find(id.value());
    return found == nodes_.end() ? nullptr : &found->second;
}

const NetworkEdge* SpatialNetwork::find_edge(NetworkEdgeId id) const noexcept {
    const auto found = edges_.find(id.value());
    return found == edges_.end() ? nullptr : &found->second;
}

const NetworkPort* SpatialNetwork::find_port(NetworkPortId id) const noexcept {
    const auto found = ports_.find(id.value());
    return found == ports_.end() ? nullptr : &found->second;
}

std::vector<const NetworkNode*> SpatialNetwork::nodes() const {
    std::vector<const NetworkNode*> result;
    result.reserve(nodes_.size());
    for (const auto& [_, node] : nodes_) {
        result.push_back(&node);
    }
    return result;
}

std::vector<NetworkNodeId> SpatialNetwork::neighbors(NetworkNodeId node_id) const {
    std::vector<NetworkNodeId> result;
    if (!find_node(node_id)) {
        return result;
    }

    for (const auto& [_, edge] : edges_) {
        if (edge.blocked) {
            continue;
        }
        if (edge.a == node_id) {
            result.push_back(edge.b);
        } else if (edge.b == node_id) {
            result.push_back(edge.a);
        }
    }

    return result;
}

bool SpatialNetwork::can_reach(NetworkNodeId start, NetworkNodeId goal) const {
    if (!find_node(start) || !find_node(goal)) {
        return false;
    }
    if (start == goal) {
        return true;
    }

    std::queue<NetworkNodeId> queue;
    std::unordered_set<std::uint64_t> visited;
    queue.push(start);
    visited.insert(start.value());

    while (!queue.empty()) {
        const auto current = queue.front();
        queue.pop();

        for (const auto neighbor : neighbors(current)) {
            if (neighbor == goal) {
                return true;
            }
            if (visited.insert(neighbor.value()).second) {
                queue.push(neighbor);
            }
        }
    }

    return false;
}

LogisticsRouteEffects SpatialNetwork::route_effects(NetworkNodeId start, NetworkNodeId goal) const {
    LogisticsRouteEffects result;
    if (!find_node(start) || !find_node(goal))
        return result;
    if (start == goal) {
        result.reachable = true;
        result.cart_speed_per_mille = 1000;
        result.animal_stamina_cost_per_mille = 0;
        result.pathfinding_reliability_per_mille = 1000;
        result.travel_safety_per_mille = 1000;
        result.corpse_recovery_per_mille = 1000;
        result.weather_resistance_per_mille = 1000;
        result.bottleneck_capacity = find_node(start)->capacity;
        return result;
    }

    struct RouteState {
        NetworkNodeId node;
        std::uint32_t edge_count = 0;
        std::uint64_t quality_sum = 0;
        std::uint32_t minimum_quality = 1000;
        std::uint32_t bottleneck = std::numeric_limits<std::uint32_t>::max();
    };
    std::queue<RouteState> queue;
    std::unordered_set<std::uint64_t> visited{start.value()};
    queue.push({start, 0, 0, 1000, find_node(start)->capacity});
    while (!queue.empty()) {
        auto route = queue.front();
        queue.pop();
        for (const auto& [_, edge] : edges_) {
            if (edge.blocked || (edge.a != route.node && edge.b != route.node))
                continue;
            const auto next = edge.a == route.node ? edge.b : edge.a;
            if (!visited.insert(next.value()).second)
                continue;
            auto next_route = route;
            next_route.node = next;
            ++next_route.edge_count;
            next_route.quality_sum += std::min(edge.quality, 1000U);
            next_route.minimum_quality = std::min(next_route.minimum_quality, edge.quality);
            next_route.bottleneck =
                std::min({next_route.bottleneck, edge.capacity, find_node(next)->capacity});
            if (next == goal) {
                const auto average =
                    static_cast<std::uint32_t>(next_route.quality_sum / next_route.edge_count);
                result.reachable = true;
                result.edge_count = next_route.edge_count;
                result.cart_speed_per_mille = std::min(1500U, 500U + average);
                result.animal_stamina_cost_per_mille = 1200U - std::min(1000U, average);
                result.pathfinding_reliability_per_mille = next_route.minimum_quality;
                result.travel_safety_per_mille = std::min(1000U, 300U + average * 7U / 10U);
                result.corpse_recovery_per_mille = std::min(1000U, 200U + average * 4U / 5U);
                result.weather_resistance_per_mille = std::min(1000U, average * 3U / 4U);
                result.bottleneck_capacity = next_route.bottleneck;
                return result;
            }
            queue.push(next_route);
        }
    }
    return result;
}

std::size_t SpatialNetwork::node_count() const noexcept {
    return nodes_.size();
}

std::size_t SpatialNetwork::edge_count() const noexcept {
    return edges_.size();
}

std::size_t SpatialNetwork::blocked_edge_count() const noexcept {
    return static_cast<std::size_t>(
        std::ranges::count_if(edges_, [](const auto& entry) { return entry.second.blocked; }));
}

std::size_t SpatialNetwork::port_count() const noexcept {
    return ports_.size();
}

std::size_t SpatialNetwork::owned_port_count() const noexcept {
    return static_cast<std::size_t>(std::ranges::count_if(
        ports_, [](const auto& entry) { return entry.second.owner_id.is_valid(); }));
}

std::size_t SpatialNetwork::sourced_port_count() const noexcept {
    return static_cast<std::size_t>(std::ranges::count_if(
        ports_, [](const auto& entry) { return entry.second.source_build_piece_id.is_valid(); }));
}

std::size_t SpatialNetwork::port_count_for_owner(core::SaveId owner_id) const noexcept {
    if (!owner_id.is_valid()) {
        return 0;
    }
    return static_cast<std::size_t>(std::ranges::count_if(
        ports_, [owner_id](const auto& entry) { return entry.second.owner_id == owner_id; }));
}

std::uint64_t SpatialNetwork::total_node_capacity() const noexcept {
    std::uint64_t total = 0;
    for (const auto& [_, node] : nodes_) {
        total += node.capacity;
    }
    return total;
}

std::uint64_t SpatialNetwork::total_edge_capacity() const noexcept {
    std::uint64_t total = 0;
    for (const auto& [_, edge] : edges_) {
        total += edge.capacity;
    }
    return total;
}

std::uint64_t SpatialNetwork::total_port_capacity() const noexcept {
    std::uint64_t total = 0;
    for (const auto& [_, port] : ports_) {
        total += port.capacity;
    }
    return total;
}

std::uint64_t SpatialNetwork::total_port_capacity_for_owner(core::SaveId owner_id) const noexcept {
    if (!owner_id.is_valid()) {
        return 0;
    }

    std::uint64_t total = 0;
    for (const auto& [_, port] : ports_) {
        if (port.owner_id == owner_id) {
            total += port.capacity;
        }
    }
    return total;
}

void SpatialNetwork::mark_dirty() noexcept {
    dirty_ = true;
}

core::Status SpatialNetwork::mark_dirty_region(dirty::DirtyRegionTracker& dirty_regions,
                                               dirty::DirtyRegionBounds bounds,
                                               std::string reason) {
    mark_dirty();
    return dirty_regions.mark(dirty_region_kind_for(kind_), bounds, std::move(reason));
}

void SpatialNetwork::clear_dirty() noexcept {
    dirty_ = false;
}

bool SpatialNetwork::is_dirty() const noexcept {
    return dirty_;
}

dirty::DirtyRegionKind dirty_region_kind_for(NetworkKind kind) noexcept {
    switch (kind) {
    case NetworkKind::road:
        return dirty::DirtyRegionKind::road_network;
    case NetworkKind::cart_access:
        return dirty::DirtyRegionKind::cart_access_network;
    case NetworkKind::storage_access:
        return dirty::DirtyRegionKind::storage_access_network;
    case NetworkKind::power:
        return dirty::DirtyRegionKind::power_network;
    case NetworkKind::ward:
        return dirty::DirtyRegionKind::ward_network;
    case NetworkKind::smoke_ventilation:
        return dirty::DirtyRegionKind::smoke_ventilation_network;
    case NetworkKind::water:
        return dirty::DirtyRegionKind::water_network;
    case NetworkKind::logistics:
        return dirty::DirtyRegionKind::logistics_network;
    }
    return dirty::DirtyRegionKind::logistics_network;
}

std::string_view network_kind_name(NetworkKind kind) noexcept {
    switch (kind) {
    case NetworkKind::road:
        return "road";
    case NetworkKind::cart_access:
        return "cart_access";
    case NetworkKind::storage_access:
        return "storage_access";
    case NetworkKind::power:
        return "power";
    case NetworkKind::ward:
        return "ward";
    case NetworkKind::smoke_ventilation:
        return "smoke_ventilation";
    case NetworkKind::water:
        return "water";
    case NetworkKind::logistics:
        return "logistics";
    }
    return "unknown";
}

NetworkKind network_kind_for_port_name(std::string_view port_name) noexcept {
    if (port_name == "road" || port_name == "road_access") {
        return NetworkKind::road;
    }
    if (port_name == "cart" || port_name == "cart_access") {
        return NetworkKind::cart_access;
    }
    if (port_name == "storage" || port_name == "storage_access" || port_name == "inventory" ||
        port_name == "item_input" || port_name == "item_output") {
        return NetworkKind::storage_access;
    }
    if (port_name == "power" || port_name == "power_input" || port_name == "power_output" ||
        port_name == "heat_input" || port_name == "heat_output") {
        return NetworkKind::power;
    }
    if (port_name == "ward" || port_name == "ward_input" || port_name == "ward_output") {
        return NetworkKind::ward;
    }
    if (port_name == "smoke" || port_name == "smoke_output" || port_name == "air_input" ||
        port_name == "ventilation") {
        return NetworkKind::smoke_ventilation;
    }
    if (port_name == "water" || port_name == "water_input" || port_name == "water_output" ||
        port_name == "fluid_input" || port_name == "irrigation") {
        return NetworkKind::water;
    }
    return NetworkKind::logistics;
}

} // namespace heartstead::networks
