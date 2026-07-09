#include "engine/player_profiles/player_profile.hpp"

#include <filesystem>
#include <iostream>
#include <string_view>

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        std::cout << "usage: heartstead_map_profile_inspector <world-root> <player-uuid>\n";
        return 0;
    }
    if (argc != 3) {
        std::cerr << "usage: heartstead_map_profile_inspector <world-root> <player-uuid>\n";
        return 2;
    }
    const auto uuid = heartstead::player_profiles::PlayerUuid::parse(argv[2]);
    if (!uuid) {
        std::cerr << "invalid player UUID\n";
        return 2;
    }

    heartstead::player_profiles::FilePlayerProfileStore store{std::filesystem::path(argv[1])};
    auto profile = store.load(*uuid);
    if (!profile) {
        std::cerr << profile.error().code << ": " << profile.error().message << '\n';
        return 1;
    }

    std::cout << "player_uuid=" << profile.value().player_uuid.value() << '\n';
    std::cout << "display_name=" << profile.value().current_display_name() << '\n';
    std::cout << "revision=" << profile.value().revision << '\n';
    std::cout << "roles=" << profile.value().roles.size() << '\n';
    std::cout << "waypoints=" << profile.value().waypoints.size() << '\n';
    std::cout << "markers=" << profile.value().personal_markers.size() << '\n';
    std::cout << "map_regions=" << profile.value().map_discovery.region_count() << '\n';
    std::cout << "discovered_cells=" << profile.value().map_discovery.discovered_count() << '\n';
    for (const auto* region : profile.value().map_discovery.regions()) {
        std::cout << "region=" << region->layer_id << '|' << region->coord.x << '|'
                  << region->coord.z << " revision=" << region->revision
                  << " cells=" << region->discovered_count() << '\n';
    }
    return 0;
}
