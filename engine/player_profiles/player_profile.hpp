#pragma once

#include "engine/core/result.hpp"
#include "engine/player_profiles/map_discovery.hpp"
#include "engine/world/coords/world_coords.hpp"

#include <compare>
#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace heartstead::player_profiles {

class PlayerUuid {
  public:
    [[nodiscard]] static std::optional<PlayerUuid> parse(std::string_view value);

    PlayerUuid() = default;

    [[nodiscard]] bool is_valid() const noexcept;
    [[nodiscard]] const std::string& value() const noexcept;

    friend auto operator<=>(const PlayerUuid&, const PlayerUuid&) = default;

  private:
    explicit PlayerUuid(std::string value) : value_(std::move(value)) {}

    std::string value_;
};

struct PlayerMapMarker {
    std::string id;
    std::string label;
    std::string layer_id = "surface";
    world::BlockCoord position;
    bool private_to_player = true;

    [[nodiscard]] core::Status validate() const;
};

struct PlayerProfile {
    PlayerUuid player_uuid;
    std::vector<std::string> display_names_history;
    std::vector<std::string> roles;
    std::optional<world::BlockCoord> spawn;
    std::optional<world::BlockCoord> bed;
    MapDiscovery map_discovery;
    std::vector<PlayerMapMarker> waypoints;
    std::vector<PlayerMapMarker> personal_markers;
    std::vector<std::string> handbook_flags;
    std::vector<std::string> progression_flags;
    std::map<std::string, std::string> world_settings;
    std::string character_data;
    std::uint64_t revision = 1;

    [[nodiscard]] core::Status validate() const;
    [[nodiscard]] std::string_view current_display_name() const noexcept;
    [[nodiscard]] core::Status remember_display_name(std::string name);
};

class PlayerProfileTextCodec {
  public:
    [[nodiscard]] static std::string encode(const PlayerProfile& profile);
    [[nodiscard]] static core::Result<PlayerProfile> decode(std::string_view text);
};

class FilePlayerProfileStore {
  public:
    explicit FilePlayerProfileStore(std::filesystem::path world_root);

    [[nodiscard]] const std::filesystem::path& world_root() const noexcept;
    [[nodiscard]] std::filesystem::path profile_directory(const PlayerUuid& uuid) const;
    [[nodiscard]] core::Status save(const PlayerProfile& profile) const;
    [[nodiscard]] core::Result<PlayerProfile> load(const PlayerUuid& uuid) const;
    [[nodiscard]] core::Result<PlayerProfile>
    load_or_create(const PlayerUuid& uuid, std::string initial_display_name) const;
    [[nodiscard]] core::Result<std::vector<PlayerUuid>> list_profiles() const;

  private:
    [[nodiscard]] core::Status save_unlocked(const PlayerProfile& profile) const;
    [[nodiscard]] core::Result<PlayerProfile> load_unlocked(const PlayerUuid& uuid) const;

    std::filesystem::path world_root_;
    mutable std::mutex mutex_;
};

} // namespace heartstead::player_profiles
