#include "engine/world/regions/region_graph.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace heartstead::world {

namespace {

[[nodiscard]] bool is_finite(double value) noexcept {
    return std::isfinite(value);
}

[[nodiscard]] bool is_normalized(double value) noexcept {
    return is_finite(value) && value >= 0.0 && value <= 1.0;
}

[[nodiscard]] core::Status require_token(std::string_view value, std::string_view field_name) {
    if (core::is_valid_local_id(value)) {
        return core::Status::ok();
    }
    return core::Status::failure("region_graph.invalid_token",
                                 std::string(field_name) + " must be a valid local id token");
}

[[nodiscard]] core::Status validate_token_list(const std::vector<std::string>& values,
                                               std::string_view field_name) {
    for (const auto& value : values) {
        auto status = require_token(value, field_name);
        if (!status) {
            return status;
        }
    }
    return core::Status::ok();
}

[[nodiscard]] std::string connection_key(std::string_view first, std::string_view second,
                                         std::string_view kind) {
    if (second < first) {
        std::swap(first, second);
    }
    return std::string(first) + "|" + std::string(second) + "|" + std::string(kind);
}

} // namespace

core::Status RegionResourceRule::validate() const {
    if (!prototype_id.is_valid()) {
        return core::Status::failure("region_graph.invalid_resource",
                                     "region resource rule prototype id is invalid");
    }
    auto status = require_token(placement, "resource placement");
    if (!status) {
        return status;
    }
    if (!is_finite(abundance) || abundance <= 0.0) {
        return core::Status::failure("region_graph.invalid_abundance",
                                     "region resource abundance must be a positive number");
    }
    return core::Status::ok();
}

core::Status RegionDescriptor::validate() const {
    auto status = require_token(id, "region id");
    if (!status) {
        return status;
    }
    status = require_token(age, "region age");
    if (!status) {
        return status;
    }
    status = require_token(biome_cluster, "region biome cluster");
    if (!status) {
        return status;
    }
    status = validate_token_list(sub_biomes, "region sub-biome");
    if (!status) {
        return status;
    }
    status = validate_token_list(future_tool_layers, "region future-tool layer");
    if (!status) {
        return status;
    }
    status = validate_token_list(mastery_return_layers, "region mastery-return layer");
    if (!status) {
        return status;
    }

    for (const auto& rule : resource_rules) {
        status = rule.validate();
        if (!status) {
            return status;
        }
    }

    if (!is_normalized(danger_gradient)) {
        return core::Status::failure("region_graph.invalid_gradient",
                                     "region danger gradient must be between 0 and 1");
    }
    if (!is_normalized(magic_gradient)) {
        return core::Status::failure("region_graph.invalid_gradient",
                                     "region magic gradient must be between 0 and 1");
    }

    for (const auto& [key, value] : ecology_parameters) {
        status = require_token(key, "region ecology parameter");
        if (!status) {
            return status;
        }
        if (!is_normalized(value)) {
            return core::Status::failure("region_graph.invalid_ecology_parameter",
                                         "region ecology parameter values must be between 0 and 1");
        }
    }
    return core::Status::ok();
}

core::Status RegionConnection::validate_fields() const {
    auto status = require_token(from_region, "from region");
    if (!status) {
        return status;
    }
    status = require_token(to_region, "to region");
    if (!status) {
        return status;
    }
    status = require_token(connection_kind, "region connection kind");
    if (!status) {
        return status;
    }
    if (from_region == to_region) {
        return core::Status::failure("region_graph.self_connection",
                                     "region connection endpoints must be different");
    }
    if (!is_finite(traversal_cost) || traversal_cost <= 0.0) {
        return core::Status::failure("region_graph.invalid_connection_cost",
                                     "region connection traversal cost must be positive");
    }
    if (!is_finite(capacity) || capacity <= 0.0) {
        return core::Status::failure("region_graph.invalid_connection_capacity",
                                     "region connection capacity must be positive");
    }
    return core::Status::ok();
}

core::Status RegionGraph::add_region(RegionDescriptor region) {
    auto status = region.validate();
    if (!status) {
        return status;
    }
    if (contains(region.id)) {
        return core::Status::failure("region_graph.duplicate_region",
                                     "duplicate region id: " + region.id);
    }

    const auto index = regions_.size();
    region_by_id_.emplace(region.id, index);
    regions_.push_back(std::move(region));
    return core::Status::ok();
}

core::Status RegionGraph::connect(RegionConnection connection) {
    auto status = connection.validate_fields();
    if (!status) {
        return status;
    }
    if (!contains(connection.from_region)) {
        return core::Status::failure("region_graph.missing_region",
                                     "connection from region does not exist: " +
                                         connection.from_region);
    }
    if (!contains(connection.to_region)) {
        return core::Status::failure("region_graph.missing_region",
                                     "connection to region does not exist: " +
                                         connection.to_region);
    }
    if (has_connection(connection.from_region, connection.to_region, connection.connection_kind)) {
        return core::Status::failure("region_graph.duplicate_connection",
                                     "duplicate region connection");
    }

    connections_.push_back(std::move(connection));
    index_connection(connections_.size() - 1);
    return core::Status::ok();
}

RegionDescriptor* RegionGraph::find(std::string_view id) noexcept {
    const auto found = region_by_id_.find(std::string(id));
    return found == region_by_id_.end() ? nullptr : &regions_[found->second];
}

const RegionDescriptor* RegionGraph::find(std::string_view id) const noexcept {
    const auto found = region_by_id_.find(std::string(id));
    return found == region_by_id_.end() ? nullptr : &regions_[found->second];
}

bool RegionGraph::contains(std::string_view id) const noexcept {
    return find(id) != nullptr;
}

std::vector<const RegionDescriptor*> RegionGraph::regions() const {
    std::vector<const RegionDescriptor*> result;
    result.reserve(regions_.size());
    for (const auto& region : regions_) {
        result.push_back(&region);
    }
    return result;
}

std::vector<RegionConnection> RegionGraph::connections_for(std::string_view id) const {
    std::vector<RegionConnection> result;
    const auto found = connections_by_region_.find(std::string(id));
    if (found == connections_by_region_.end()) {
        return result;
    }
    result.reserve(found->second.size());
    for (const auto index : found->second) {
        result.push_back(connections_[index]);
    }
    return result;
}

bool RegionGraph::are_connected(std::string_view first, std::string_view second) const {
    const auto found = connections_by_region_.find(std::string(first));
    if (found == connections_by_region_.end()) {
        return false;
    }
    return std::ranges::any_of(found->second, [this, second](std::size_t index) {
        const auto& connection = connections_[index];
        return connection.from_region == second || connection.to_region == second;
    });
}

std::optional<double> RegionGraph::ecology_parameter(std::string_view region_id,
                                                     std::string_view key) const {
    const auto* region = find(region_id);
    if (region == nullptr) {
        return std::nullopt;
    }
    const auto found = region->ecology_parameters.find(std::string(key));
    return found == region->ecology_parameters.end() ? std::nullopt
                                                     : std::optional<double>(found->second);
}

std::size_t RegionGraph::region_count() const noexcept {
    return regions_.size();
}

std::size_t RegionGraph::connection_count() const noexcept {
    return connections_.size();
}

bool RegionGraph::has_connection(std::string_view first, std::string_view second,
                                 std::string_view kind) const {
    const auto target_key = connection_key(first, second, kind);
    return std::ranges::any_of(connections_, [&target_key](const RegionConnection& connection) {
        return connection_key(connection.from_region, connection.to_region,
                              connection.connection_kind) == target_key;
    });
}

void RegionGraph::index_connection(std::size_t index) {
    const auto& connection = connections_[index];
    connections_by_region_[connection.from_region].push_back(index);
    connections_by_region_[connection.to_region].push_back(index);
}

} // namespace heartstead::world
