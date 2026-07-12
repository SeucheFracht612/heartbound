#include "engine/core/logging.hpp"
#include "engine/player_profiles/map_discovery.hpp"
#include "engine/player_profiles/player_profile.hpp"
#include "engine/save/save_binary_codec.hpp"
#include "engine/save/save_text_codec.hpp"
#include "engine/server_logs/server_log.hpp"
#include "engine/simulation/world_time.hpp"
#include "engine/world/world_state.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <thread>

namespace {

[[nodiscard]] std::filesystem::path temporary_root(std::string_view label) {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("heartstead_" + std::string(label) + "_" + std::to_string(nonce));
}

void test_authoritative_world_time_and_save_round_trip() {
    using namespace heartstead;

    simulation::WorldTimeConfig config;
    assert(config.validate());
    auto hour = config.ticks_per_hour();
    assert(hour);
    assert(hour.value() == 72'000);

    simulation::WorldClock clock(100);
    assert(clock.advance_hours(6, config));
    assert(clock.now() == 432'100);
    simulation::WorldClock overflowing(std::numeric_limits<std::uint64_t>::max());
    assert(!overflowing.advance(1));

    save::SaveMetadata metadata;
    metadata.game_version = "0.2-test";
    metadata.world_seed = 123;
    metadata.world_time = clock.now();
    const auto encoded_metadata = save::SaveTextCodec::encode_metadata(metadata);
    auto decoded_metadata = save::SaveTextCodec::decode_metadata(encoded_metadata);
    assert(decoded_metadata);
    assert(decoded_metadata.value().world_time == clock.now());

    save::SaveSnapshot snapshot;
    snapshot.metadata = metadata;
    const auto binary = save::SaveBinaryCodec::encode_snapshot(snapshot);
    auto decoded_binary = save::SaveBinaryCodec::decode_snapshot(binary);
    assert(decoded_binary);
    assert(decoded_binary.value().metadata.world_time == clock.now());

    const std::string legacy = "heartstead.save_text.v1\n"
                               "schema_version=1\n"
                               "game_version=0.1\n"
                               "world_seed=7\n"
                               "end\n";
    auto decoded_legacy = save::SaveTextCodec::decode_metadata(legacy);
    assert(decoded_legacy);
    assert(decoded_legacy.value().world_time == 0);

    world::WorldState state({metadata});
    assert(state.world_time() == clock.now());
    assert(state.advance_world_time(10));
    assert(state.world_time() == clock.now() + 10);
    assert(state.metadata().world_time == state.world_time());
    assert(state.advance_world_time(1));
    assert(state.metadata().world_time == clock.now() + 11);
}

void test_layered_server_map_discovery() {
    using namespace heartstead::player_profiles;

    MapDiscovery discovery;
    assert(discovery.discover("surface", {-1, -1}));
    assert(discovery.discover("surface", {63, 63}));
    assert(discovery.discover("caves", {-1, -1}));
    auto duplicate = discovery.discover("surface", {-1, -1});
    assert(duplicate);
    assert(!duplicate.value());
    assert(discovery.region_count() == 3);
    assert(discovery.discovered_count() == 3);
    assert(discovery.is_discovered("surface", {-1, -1}));
    assert(!discovery.is_discovered("underground", {-1, -1}));

    const auto negative_region = map_discovery_region_coord({-1, -65});
    const auto negative_local = map_discovery_local_coord({-1, -65});
    assert((negative_region == MapDiscoveryRegionCoord{-1, -2}));
    assert(negative_local.x == 63 && negative_local.z == 63);

    const auto encoded = MapDiscoveryTextCodec::encode(discovery);
    auto decoded = MapDiscoveryTextCodec::decode(encoded);
    assert(decoded);
    assert(decoded.value().region_count() == discovery.region_count());
    assert(decoded.value().is_discovered("caves", {-1, -1}));
    auto trailing_map = MapDiscoveryTextCodec::decode(encoded + "unexpected\n");
    assert(!trailing_map);
    assert(trailing_map.error().code == "map_discovery.trailing_data");

    MapDiscoveryRegion newer = *discovery.find_region("surface", {-1, -1});
    ++newer.revision;
    newer.discovered.fill(0);
    assert(!discovery.upsert_region(newer));
    assert(discovery.is_discovered("surface", {-1, -1}));

    MapDiscoveryRegion conflicting = *discovery.find_region("surface", {-1, -1});
    conflicting.discovered[0] ^= 1U;
    assert(!discovery.upsert_region(conflicting));
}

void test_server_side_player_profile_persistence() {
    using namespace heartstead;
    using namespace heartstead::player_profiles;

    const auto uuid = PlayerUuid::parse("123E4567-E89B-12D3-A456-426614174000");
    assert(uuid);
    assert(uuid->value() == "123e4567-e89b-12d3-a456-426614174000");
    assert(!PlayerUuid::parse("00000000-0000-0000-0000-000000000000"));

    const auto root = temporary_root("profiles_v02");
    FilePlayerProfileStore store(root);
    auto profile = store.load_or_create(*uuid, "First Settler");
    assert(profile);
    profile.value().spawn = world::BlockCoord{std::int64_t{1} << 50, -400, -9};
    profile.value().bed = world::BlockCoord{-20, 70, 5};
    profile.value().waypoints.push_back(
        {"home", "Old | Home", "surface", *profile.value().spawn, true});
    profile.value().progression_flags.push_back("first_join");
    assert(profile.value().map_discovery.discover("surface", {1024, -2048}));
    assert(profile.value().remember_display_name("Second Settler"));
    assert(store.save(profile.value()));

    auto loaded = store.load(*uuid);
    assert(loaded);
    assert(loaded.value().current_display_name() == "Second Settler");
    assert(loaded.value().spawn == profile.value().spawn);
    assert(loaded.value().waypoints.size() == 1);
    assert(loaded.value().map_discovery.is_discovered("surface", {1024, -2048}));
    const auto encoded_profile = PlayerProfileTextCodec::encode(loaded.value());
    auto trailing_profile = PlayerProfileTextCodec::decode(encoded_profile + "unexpected\n");
    assert(!trailing_profile);
    assert(trailing_profile.error().code == "player_profile.trailing_data");
    assert(std::filesystem::exists(store.profile_directory(*uuid) / "profile.dat"));
    assert(std::filesystem::exists(store.profile_directory(*uuid) / "map_discovery.dat"));

    auto profiles = store.list_profiles();
    assert(profiles);
    assert(profiles.value().size() == 1);
    assert(profiles.value().front() == *uuid);

    const auto concurrent_uuid = PlayerUuid::parse("123e4567-e89b-12d3-a456-426614174001");
    assert(concurrent_uuid);
    std::string first_result_name;
    std::string second_result_name;
    std::thread first([&] {
        auto created = store.load_or_create(*concurrent_uuid, "Concurrent One");
        assert(created);
        first_result_name = std::string(created.value().current_display_name());
    });
    std::thread second([&] {
        auto created = store.load_or_create(*concurrent_uuid, "Concurrent Two");
        assert(created);
        second_result_name = std::string(created.value().current_display_name());
    });
    first.join();
    second.join();
    assert(first_result_name == second_result_name);

    assert(std::filesystem::remove(store.profile_directory(*uuid) / "map_discovery.dat"));
    auto incomplete = store.load(*uuid);
    assert(!incomplete);
    assert(incomplete.error().code == "player_profile.missing_map_discovery");
    std::filesystem::remove_all(root);
}

void test_append_only_admin_and_chat_logs() {
    using namespace heartstead;
    using namespace heartstead::server_logs;

    const auto uuid = player_profiles::PlayerUuid::parse("123e4567-e89b-12d3-a456-426614174000");
    assert(uuid);
    const auto root = temporary_root("logs_v02") / "server";
    FileServerLog logs(root);
    assert(logs.append_join(*uuid, "Settler", "sha256:abc", 100, "Day 1 08:00", "session_1"));
    assert(logs.append_chat(*uuid, "Settler", "local", "raw | text = stays\nwhole\x1b[31m", 101,
                            "Day 1 08:01", "session_1", {{"moderated", "false"}}));
    assert(logs.append_leave(*uuid, "Settler", 102, "Day 1 08:02", "session_1"));

    auto chat = logs.query_current(ServerLogCategory::chat);
    assert(chat);
    assert(chat.value().size() == 1);
    assert(chat.value().front().message == "raw | text = stays\nwhole\x1b[31m");
    assert(chat.value().front().world_time == 101);
    assert(chat.value().front().real_timestamp_utc.ends_with('Z'));
    auto invalid_date = chat.value().front();
    invalid_date.real_timestamp_utc = "2026-99-09T20:57:31.000Z";
    assert(!ServerLogLineCodec::decode(ServerLogLineCodec::encode(invalid_date)));
    auto excessive_metadata = chat.value().front();
    for (std::size_t index = 0; index < 257; ++index)
        excessive_metadata.metadata.emplace("field_" + std::to_string(index), "value");
    assert(!excessive_metadata.validate());

    ServerLogFilter join_filter;
    join_filter.event_type = "join";
    join_filter.player_uuid = *uuid;
    auto joins = logs.query_current(ServerLogCategory::general, join_filter);
    assert(joins);
    assert(joins.value().size() == 1);
    assert(joins.value().front().metadata.at("address_hash") == "sha256:abc");
    assert(std::filesystem::exists(root / "logs" / "current.log"));
    assert(std::filesystem::exists(root / "logs" / "chat" / "current.log"));
    {
        std::ifstream input(root / "logs" / "chat" / "current.log", std::ios::binary);
        const std::string stored((std::istreambuf_iterator<char>(input)),
                                 std::istreambuf_iterator<char>());
        assert(!stored.contains('\x1b'));
        assert(stored.contains("%1B"));
    }
    std::filesystem::remove_all(root.parent_path());
}

void test_log_sink_may_reconfigure_itself() {
    using namespace heartstead::core;
    bool called = false;
    set_log_sink([&called](LogLevel, std::string_view) {
        called = true;
        reset_log_sink();
        log(LogLevel::debug, "nested after reset");
    });
    log(LogLevel::info, "outer");
    assert(called);
    reset_log_sink();
}

} // namespace

int main() {
    test_authoritative_world_time_and_save_round_trip();
    test_layered_server_map_discovery();
    test_server_side_player_profile_persistence();
    test_append_only_admin_and_chat_logs();
    test_log_sink_may_reconfigure_itself();
    return 0;
}
