#pragma once

#include "engine/core/ids.hpp"
#include "engine/core/result.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace heartstead::world {

struct RegionResourceRule {
    core::PrototypeId prototype_id;
    std::string placement;
    double abundance = 1.0;

    [[nodiscard]] core::Status validate() const;
};

struct RegionDescriptor {
    std::string id;
    std::string age;
    std::string biome_cluster;
    std::vector<std::string> sub_biomes;
    std::vector<RegionResourceRule> resource_rules;
    double danger_gradient = 0.0;
    double magic_gradient = 0.0;
    std::vector<std::string> future_tool_layers;
    std::vector<std::string> mastery_return_layers;
    std::unordered_map<std::string, double> ecology_parameters;

    [[nodiscard]] core::Status validate() const;
};

struct RegionConnection {
    std::string from_region;
    std::string to_region;
    std::string connection_kind = "adjacent";
    double traversal_cost = 1.0;
    double capacity = 1.0;

    [[nodiscard]] core::Status validate_fields() const;
};

class RegionGraph {
  public:
    [[nodiscard]] core::Status add_region(RegionDescriptor region);
    [[nodiscard]] core::Status connect(RegionConnection connection);

    [[nodiscard]] RegionDescriptor* find(std::string_view id) noexcept;
    [[nodiscard]] const RegionDescriptor* find(std::string_view id) const noexcept;
    [[nodiscard]] bool contains(std::string_view id) const noexcept;
    [[nodiscard]] std::vector<const RegionDescriptor*> regions() const;
    [[nodiscard]] std::vector<RegionConnection> connections_for(std::string_view id) const;
    [[nodiscard]] bool are_connected(std::string_view first, std::string_view second) const;
    [[nodiscard]] std::optional<double> ecology_parameter(std::string_view region_id,
                                                          std::string_view key) const;
    [[nodiscard]] std::size_t region_count() const noexcept;
    [[nodiscard]] std::size_t connection_count() const noexcept;

  private:
    [[nodiscard]] bool has_connection(std::string_view first, std::string_view second,
                                      std::string_view kind) const;
    void index_connection(std::size_t index);

    std::vector<RegionDescriptor> regions_;
    std::vector<RegionConnection> connections_;
    std::unordered_map<std::string, std::size_t> region_by_id_;
    std::unordered_map<std::string, std::vector<std::size_t>> connections_by_region_;
};

} // namespace heartstead::world
