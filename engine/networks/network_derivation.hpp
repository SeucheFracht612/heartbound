#pragma once

#include "engine/assemblies/assembly.hpp"
#include "engine/build/build_piece.hpp"
#include "engine/core/result.hpp"
#include "engine/networks/spatial_network.hpp"

#include <cstddef>
#include <vector>

namespace heartstead::networks {

struct SpatialNetworkDerivationInput {
    std::vector<const build::BuildPieceRecord*> build_pieces;
    std::vector<const assemblies::AssemblyRecord*> assemblies;
};

struct SpatialNetworkDerivationStats {
    std::size_t network_count = 0;
    std::size_t build_piece_port_count = 0;
    std::size_t assembly_port_count = 0;
    std::size_t skipped_incomplete_build_piece_count = 0;
    std::size_t generated_node_count = 0;
    std::size_t generated_edge_count = 0;
    std::size_t generated_port_count = 0;
};

struct SpatialNetworkDerivationResult {
    SpatialNetworkDerivationStats stats;
    std::vector<SpatialNetwork> networks;
};

class SpatialNetworkDeriver {
  public:
    [[nodiscard]] static core::Result<SpatialNetworkDerivationResult>
    derive(const SpatialNetworkDerivationInput& input);
};

} // namespace heartstead::networks
