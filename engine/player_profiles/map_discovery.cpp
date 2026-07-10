#include "engine/player_profiles/map_discovery.hpp"

#include "engine/core/ids.hpp"

#include <bit>
#include <charconv>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <system_error>
#include <utility>

namespace heartstead::player_profiles {

namespace {

constexpr std::string_view magic = "heartstead.map_discovery.v1";

[[nodiscard]] std::int64_t floor_div(std::int64_t value, std::int64_t divisor) noexcept {
    auto quotient = value / divisor;
    const auto remainder = value % divisor;
    if (remainder < 0) {
        --quotient;
    }
    return quotient;
}

[[nodiscard]] std::uint16_t floor_mod(std::int64_t value, std::int64_t divisor) noexcept {
    auto remainder = value % divisor;
    if (remainder < 0) {
        remainder += divisor;
    }
    return static_cast<std::uint16_t>(remainder);
}

[[nodiscard]] bool valid_local(MapDiscoveryLocalCoord local) noexcept {
    return local.x < map_discovery_region_edge && local.z < map_discovery_region_edge;
}

[[nodiscard]] std::size_t bit_index(MapDiscoveryLocalCoord local) noexcept {
    return static_cast<std::size_t>(local.z) * map_discovery_region_edge + local.x;
}

[[nodiscard]] std::vector<std::string_view> split(std::string_view value, char delimiter) {
    std::vector<std::string_view> result;
    std::size_t start = 0;
    while (start <= value.size()) {
        const auto end = value.find(delimiter, start);
        if (end == std::string_view::npos) {
            result.push_back(value.substr(start));
            break;
        }
        result.push_back(value.substr(start, end - start));
        start = end + 1;
    }
    return result;
}

template <typename T>
[[nodiscard]] core::Result<T> parse_integer(std::string_view value, std::string_view label) {
    T parsed{};
    const auto [ptr, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (error != std::errc{} || ptr != value.data() + value.size()) {
        return core::Result<T>::failure("map_discovery.invalid_number",
                                        "invalid map discovery number: " + std::string(label));
    }
    return core::Result<T>::success(parsed);
}

[[nodiscard]] std::string encode_words(const MapDiscoveryRegion& region) {
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (const auto word : region.discovered) {
        output << std::setw(16) << word;
    }
    return output.str();
}

[[nodiscard]] core::Result<std::array<std::uint64_t, map_discovery_word_count>>
decode_words(std::string_view value) {
    constexpr std::size_t word_chars = 16;
    if (value.size() != map_discovery_word_count * word_chars) {
        return core::Result<std::array<std::uint64_t, map_discovery_word_count>>::failure(
            "map_discovery.invalid_mask", "map discovery mask has the wrong encoded size");
    }

    std::array<std::uint64_t, map_discovery_word_count> words{};
    for (std::size_t index = 0; index < words.size(); ++index) {
        const auto word = value.substr(index * word_chars, word_chars);
        const auto [ptr, error] =
            std::from_chars(word.data(), word.data() + word.size(), words[index], 16);
        if (error != std::errc{} || ptr != word.data() + word.size()) {
            return core::Result<std::array<std::uint64_t, map_discovery_word_count>>::failure(
                "map_discovery.invalid_mask", "map discovery mask contains invalid hex data");
        }
    }
    return core::Result<std::array<std::uint64_t, map_discovery_word_count>>::success(words);
}

} // namespace

core::Status MapDiscoveryRegion::validate() const {
    if (!core::is_valid_local_id(layer_id)) {
        return core::Status::failure("map_discovery.invalid_layer",
                                     "map discovery layer id must be a valid local id");
    }
    return core::Status::ok();
}

bool MapDiscoveryRegion::is_discovered(MapDiscoveryLocalCoord local) const noexcept {
    if (!valid_local(local)) {
        return false;
    }
    const auto index = bit_index(local);
    return (discovered[index / 64] & (std::uint64_t{1} << (index % 64))) != 0;
}

core::Result<bool> MapDiscoveryRegion::discover(MapDiscoveryLocalCoord local) noexcept {
    if (!valid_local(local)) {
        return core::Result<bool>::failure("map_discovery.local_out_of_bounds",
                                           "map discovery local coordinate is outside 64x64");
    }
    const auto index = bit_index(local);
    const auto mask = std::uint64_t{1} << (index % 64);
    auto& word = discovered[index / 64];
    if ((word & mask) != 0) {
        return core::Result<bool>::success(false);
    }
    if (revision == std::numeric_limits<std::uint64_t>::max()) {
        return core::Result<bool>::failure("map_discovery.revision_overflow",
                                           "map discovery revision cannot overflow u64");
    }
    word |= mask;
    ++revision;
    return core::Result<bool>::success(true);
}

std::size_t MapDiscoveryRegion::discovered_count() const noexcept {
    std::size_t result = 0;
    for (const auto word : discovered) {
        result += static_cast<std::size_t>(std::popcount(word));
    }
    return result;
}

core::Result<bool> MapDiscovery::discover(std::string_view layer_id, MapCellCoord cell) {
    if (!core::is_valid_local_id(layer_id)) {
        return core::Result<bool>::failure("map_discovery.invalid_layer",
                                           "map discovery layer id must be a valid local id");
    }
    const auto region_coord = map_discovery_region_coord(cell);
    auto* region = find_region_mutable(layer_id, region_coord);
    if (region == nullptr) {
        MapDiscoveryRegion created;
        created.coord = region_coord;
        created.layer_id = std::string(layer_id);
        const auto [it, inserted] =
            regions_.emplace(Key{created.layer_id, created.coord}, std::move(created));
        if (!inserted) {
            return core::Result<bool>::failure("map_discovery.insert_failed",
                                               "failed to create map discovery region");
        }
        region = &it->second;
    }
    return region->discover(map_discovery_local_coord(cell));
}

bool MapDiscovery::is_discovered(std::string_view layer_id, MapCellCoord cell) const noexcept {
    const auto* region = find_region(layer_id, map_discovery_region_coord(cell));
    return region != nullptr && region->is_discovered(map_discovery_local_coord(cell));
}

core::Status MapDiscovery::upsert_region(MapDiscoveryRegion region) {
    auto status = region.validate();
    if (!status) {
        return status;
    }
    const Key key{region.layer_id, region.coord};
    const auto found = regions_.find(key);
    if (found != regions_.end()) {
        if (region.revision < found->second.revision) {
            return core::Status::failure("map_discovery.stale_revision",
                                         "map discovery update is older than stored region");
        }
        if (region.revision == found->second.revision) {
            if (region.discovered != found->second.discovered) {
                return core::Status::failure(
                    "map_discovery.conflicting_revision",
                    "equal map discovery revisions must contain identical masks");
            }
            return core::Status::ok();
        }
        for (std::size_t index = 0; index < region.discovered.size(); ++index) {
            if ((found->second.discovered[index] & ~region.discovered[index]) != 0) {
                return core::Status::failure(
                    "map_discovery.non_monotonic_update",
                    "map discovery updates cannot clear previously discovered cells");
            }
        }
    }
    regions_.insert_or_assign(key, std::move(region));
    return core::Status::ok();
}

const MapDiscoveryRegion* MapDiscovery::find_region(std::string_view layer_id,
                                                    MapDiscoveryRegionCoord coord) const noexcept {
    const auto found = regions_.find(Key{std::string(layer_id), coord});
    return found == regions_.end() ? nullptr : &found->second;
}

MapDiscoveryRegion* MapDiscovery::find_region_mutable(std::string_view layer_id,
                                                      MapDiscoveryRegionCoord coord) noexcept {
    const auto found = regions_.find(Key{std::string(layer_id), coord});
    return found == regions_.end() ? nullptr : &found->second;
}

std::vector<const MapDiscoveryRegion*> MapDiscovery::regions() const {
    std::vector<const MapDiscoveryRegion*> result;
    result.reserve(regions_.size());
    for (const auto& [_, region] : regions_) {
        result.push_back(&region);
    }
    return result;
}

std::size_t MapDiscovery::region_count() const noexcept {
    return regions_.size();
}

std::size_t MapDiscovery::discovered_count() const noexcept {
    std::size_t result = 0;
    for (const auto& [_, region] : regions_) {
        result += region.discovered_count();
    }
    return result;
}

std::size_t MapDiscovery::clear_layer(std::string_view layer_id) {
    std::size_t removed = 0;
    for (auto it = regions_.begin(); it != regions_.end();) {
        if (it->first.layer_id == layer_id) {
            it = regions_.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }
    return removed;
}

void MapDiscovery::clear() noexcept {
    regions_.clear();
}

std::string MapDiscoveryTextCodec::encode(const MapDiscovery& discovery) {
    std::ostringstream output;
    output << magic << '\n';
    for (const auto* region : discovery.regions()) {
        output << "region=" << region->layer_id << '|' << region->coord.x << '|' << region->coord.z
               << '|' << region->revision << '|' << encode_words(*region) << '\n';
    }
    output << "end\n";
    return output.str();
}

core::Result<MapDiscovery> MapDiscoveryTextCodec::decode(std::string_view text) {
    MapDiscovery result;
    bool saw_magic = false;
    bool saw_end = false;
    std::size_t consumed_bytes = 0;
    std::set<std::string> seen_keys;

    std::size_t line_start = 0;
    while (line_start <= text.size()) {
        const auto line_end = text.find('\n', line_start);
        auto line = line_end == std::string_view::npos
                        ? text.substr(line_start)
                        : text.substr(line_start, line_end - line_start);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }

        if (!saw_magic) {
            if (line != magic) {
                return core::Result<MapDiscovery>::failure(
                    "map_discovery.invalid_magic", "map discovery file has an invalid header");
            }
            saw_magic = true;
        } else if (line == "end") {
            saw_end = true;
            consumed_bytes = line_end == std::string_view::npos ? text.size() : line_end + 1;
            break;
        } else if (!line.empty()) {
            if (!line.starts_with("region=")) {
                return core::Result<MapDiscovery>::failure(
                    "map_discovery.invalid_line", "map discovery file contains an unknown record");
            }
            const auto parts = split(line.substr(7), '|');
            if (parts.size() != 5 || !core::is_valid_local_id(parts[0])) {
                return core::Result<MapDiscovery>::failure(
                    "map_discovery.invalid_region", "map discovery region record is malformed");
            }
            auto x = parse_integer<std::int64_t>(parts[1], "region_x");
            auto z = parse_integer<std::int64_t>(parts[2], "region_z");
            auto revision = parse_integer<std::uint64_t>(parts[3], "revision");
            auto words = decode_words(parts[4]);
            if (!x || !z || !revision || !words) {
                return core::Result<MapDiscovery>::failure(
                    "map_discovery.invalid_region", "map discovery region record is malformed");
            }
            const auto key = std::string(parts[0]) + "|" + std::to_string(x.value()) + "|" +
                             std::to_string(z.value());
            if (!seen_keys.insert(key).second) {
                return core::Result<MapDiscovery>::failure("map_discovery.duplicate_region",
                                                           "map discovery region is duplicated");
            }
            MapDiscoveryRegion region;
            region.layer_id = std::string(parts[0]);
            region.coord = {x.value(), z.value()};
            region.revision = revision.value();
            region.discovered = words.value();
            auto status = result.upsert_region(std::move(region));
            if (!status) {
                return core::Result<MapDiscovery>::failure(status.error().code,
                                                           status.error().message);
            }
        }

        if (line_end == std::string_view::npos) {
            break;
        }
        line_start = line_end + 1;
    }

    if (!saw_magic || !saw_end) {
        return core::Result<MapDiscovery>::failure(
            "map_discovery.incomplete", "map discovery file is missing required markers");
    }
    if (consumed_bytes != text.size()) {
        return core::Result<MapDiscovery>::failure(
            "map_discovery.trailing_data", "map discovery file contains data after end marker");
    }
    return core::Result<MapDiscovery>::success(std::move(result));
}

MapDiscoveryRegionCoord map_discovery_region_coord(MapCellCoord cell) noexcept {
    constexpr auto edge = static_cast<std::int64_t>(map_discovery_region_edge);
    return {floor_div(cell.x, edge), floor_div(cell.z, edge)};
}

MapDiscoveryLocalCoord map_discovery_local_coord(MapCellCoord cell) noexcept {
    constexpr auto edge = static_cast<std::int64_t>(map_discovery_region_edge);
    return {floor_mod(cell.x, edge), floor_mod(cell.z, edge)};
}

} // namespace heartstead::player_profiles
