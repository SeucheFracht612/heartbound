#pragma once

#include "engine/core/result.hpp"

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace heartstead::player_profiles {

inline constexpr std::uint16_t map_discovery_region_edge = 64;
inline constexpr std::size_t map_discovery_word_count =
    (static_cast<std::size_t>(map_discovery_region_edge) * map_discovery_region_edge) / 64;

struct MapCellCoord {
    std::int64_t x = 0;
    std::int64_t z = 0;

    friend auto operator<=>(const MapCellCoord&, const MapCellCoord&) = default;
};

struct MapDiscoveryRegionCoord {
    std::int64_t x = 0;
    std::int64_t z = 0;

    friend auto operator<=>(const MapDiscoveryRegionCoord&,
                            const MapDiscoveryRegionCoord&) = default;
};

struct MapDiscoveryLocalCoord {
    std::uint16_t x = 0;
    std::uint16_t z = 0;
};

struct MapDiscoveryRegion {
    MapDiscoveryRegionCoord coord;
    std::string layer_id = "surface";
    std::array<std::uint64_t, map_discovery_word_count> discovered{};
    std::uint64_t revision = 0;

    [[nodiscard]] core::Status validate() const;
    [[nodiscard]] bool is_discovered(MapDiscoveryLocalCoord local) const noexcept;
    [[nodiscard]] core::Result<bool> discover(MapDiscoveryLocalCoord local) noexcept;
    [[nodiscard]] std::size_t discovered_count() const noexcept;
};

class MapDiscovery {
  public:
    [[nodiscard]] core::Result<bool> discover(std::string_view layer_id, MapCellCoord cell);
    [[nodiscard]] bool is_discovered(std::string_view layer_id, MapCellCoord cell) const noexcept;
    [[nodiscard]] core::Status upsert_region(MapDiscoveryRegion region);

    [[nodiscard]] const MapDiscoveryRegion*
    find_region(std::string_view layer_id, MapDiscoveryRegionCoord coord) const noexcept;
    [[nodiscard]] std::vector<const MapDiscoveryRegion*> regions() const;
    [[nodiscard]] std::size_t region_count() const noexcept;
    [[nodiscard]] std::size_t discovered_count() const noexcept;
    [[nodiscard]] std::size_t clear_layer(std::string_view layer_id);
    void clear() noexcept;

  private:
    struct Key {
        std::string layer_id;
        MapDiscoveryRegionCoord coord;

        friend auto operator<=>(const Key&, const Key&) = default;
    };

    [[nodiscard]] MapDiscoveryRegion* find_region_mutable(std::string_view layer_id,
                                                          MapDiscoveryRegionCoord coord) noexcept;

    std::map<Key, MapDiscoveryRegion> regions_;
};

class MapDiscoveryTextCodec {
  public:
    [[nodiscard]] static std::string encode(const MapDiscovery& discovery);
    [[nodiscard]] static core::Result<MapDiscovery> decode(std::string_view text);
};

[[nodiscard]] MapDiscoveryRegionCoord map_discovery_region_coord(MapCellCoord cell) noexcept;
[[nodiscard]] MapDiscoveryLocalCoord map_discovery_local_coord(MapCellCoord cell) noexcept;

} // namespace heartstead::player_profiles
