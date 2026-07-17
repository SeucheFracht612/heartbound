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
#include <utility>
#include <vector>

namespace {

[[nodiscard]] std::filesystem::path temporary_root(std::string_view label) {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("heartstead_" + std::string(label) + "_" + std::to_string(nonce));
}

[[nodiscard]] heartstead::server_logs::ServerLogEntry
log_entry(std::string timestamp, heartstead::simulation::WorldTick world_time,
          std::string message = {}) {
    heartstead::server_logs::ServerLogEntry entry;
    entry.real_timestamp_utc = std::move(timestamp);
    entry.world_time = world_time;
    entry.world_calendar = "Day 1";
    entry.event_type = "test_event";
    entry.server_session = "test_session";
    entry.message = std::move(message);
    return entry;
}

void write_archive(const std::filesystem::path& path,
                   const heartstead::server_logs::ServerLogEntry& entry) {
    auto text = heartstead::server_logs::ServerLogLineCodec::encode(entry);
    text.push_back('\n');
    const auto bytes = heartstead::server_logs::ServerLogArchiveCodec::encode(text);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    assert(output);
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    output.close();
    assert(output);
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
    assert(binary);
    auto decoded_binary = save::SaveBinaryCodec::decode_snapshot(binary.value());
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

void test_server_log_rotation_and_archive_ordering() {
    using namespace heartstead;
    using namespace heartstead::server_logs;

    const auto root = temporary_root("log_rotation") / "server";
    FileServerLog logs(root, {.rotate_after_bytes = 1024U * 1024U, .rotate_daily = true});
    assert(logs.append(ServerLogCategory::general,
                       log_entry("2026-07-16T23:59:59.999Z", 1, "before")));
    assert(
        logs.append(ServerLogCategory::general, log_entry("2026-07-17T00:00:00.000Z", 2, "after")));

    auto current = logs.query_current(ServerLogCategory::general);
    assert(current && current.value().size() == 1);
    assert(current.value().front().world_time == 2);
    auto all = logs.query_all(ServerLogCategory::general);
    assert(all && all.value().size() == 2);
    assert(all.value()[0].world_time == 1);
    assert(all.value()[1].world_time == 2);

    std::ifstream index(root / "logs" / "rotated" / "index.log", std::ios::binary);
    std::string index_line;
    assert(index && std::getline(index, index_line));
    assert(index_line.contains("|server|2026-07-16|2026-07-17|"));
    std::filesystem::remove_all(root.parent_path());

    const auto ordered_root = temporary_root("log_archive_order") / "server";
    const auto rotated = ordered_root / "logs" / "rotated";
    std::filesystem::create_directories(rotated);
    write_archive(rotated / "20260717T120000000Z.chat.10.log.hsz",
                  log_entry("2026-07-17T12:00:00.010Z", 10));
    write_archive(rotated / "20260717T120000000Z.chat.2.log.hsz",
                  log_entry("2026-07-17T12:00:00.002Z", 2));
    write_archive(rotated / "20260717T120000000Z.server.1.log.hsz",
                  log_entry("2026-07-17T12:00:00.001Z", 99));
    FileServerLog ordered_logs(ordered_root);
    auto ordered = ordered_logs.query_all(ServerLogCategory::chat);
    assert(ordered && ordered.value().size() == 2);
    assert(ordered.value()[0].world_time == 2);
    assert(ordered.value()[1].world_time == 10);
    std::filesystem::remove_all(ordered_root.parent_path());
}

void test_server_log_corruption_limits_and_filters_fail_closed() {
    using namespace heartstead;
    using namespace heartstead::server_logs;

    ServerLogConfig invalid_config;
    invalid_config.rotate_after_bytes = 0;
    assert(!invalid_config.validate());
    invalid_config.rotate_after_bytes = std::numeric_limits<std::uintmax_t>::max();
    assert(!invalid_config.validate());
    FileServerLog invalid_logs(temporary_root("invalid_log_config"), invalid_config);
    assert(!invalid_logs.initialize());

    const auto compressed = ServerLogArchiveCodec::encode("aaaa");
    auto decode_limited = ServerLogArchiveCodec::decode(compressed, {.maximum_output_bytes = 3});
    assert(!decode_limited);
    assert(decode_limited.error().code == "server_log.archive_too_large");

    ServerLogFilter reversed_real_time;
    reversed_real_time.real_time_from_utc = "2026-07-18T00:00:00.000Z";
    reversed_real_time.real_time_to_utc = "2026-07-17T00:00:00.000Z";
    assert(!reversed_real_time.validate());
    ServerLogFilter reversed_world_time;
    reversed_world_time.world_time_from = 2;
    reversed_world_time.world_time_to = 1;
    assert(!reversed_world_time.validate());

    auto oversized = log_entry("2026-07-17T00:00:00.000Z", 1);
    for (std::size_t index = 0; index < 17; ++index) {
        oversized.metadata.emplace("field_" + std::to_string(index), std::string(64U * 1024U, 'a'));
    }
    const auto oversized_status = oversized.validate();
    assert(!oversized_status);
    assert(oversized_status.error().code == "server_log.line_too_large");

    const auto blank_root = temporary_root("log_blank") / "server";
    std::filesystem::create_directories(blank_root / "logs");
    {
        std::ofstream output(blank_root / "logs" / "current.log", std::ios::binary);
        output << ServerLogLineCodec::encode(log_entry("2026-07-17T00:00:00.000Z", 1)) << "\n\n";
    }
    FileServerLog blank_logs(blank_root);
    auto blank_current = blank_logs.query_current(ServerLogCategory::general);
    assert(!blank_current && blank_current.error().code == "server_log.invalid_line");
    auto blank_all = blank_logs.query_all(ServerLogCategory::general);
    assert(!blank_all && blank_all.error().code == "server_log.invalid_line");
    auto invalid_filter_query =
        blank_logs.query_current(ServerLogCategory::general, reversed_real_time);
    assert(!invalid_filter_query &&
           invalid_filter_query.error().code == "server_log.invalid_filter");
    auto invalid_category = blank_logs.query_current(static_cast<ServerLogCategory>(99));
    assert(!invalid_category && invalid_category.error().code == "server_log.invalid_category");
    std::filesystem::remove_all(blank_root.parent_path());

    const auto malformed_root = temporary_root("log_bad_archive") / "server";
    const auto malformed_rotated = malformed_root / "logs" / "rotated";
    std::filesystem::create_directories(malformed_rotated);
    write_archive(malformed_rotated / "forged.chat.payload.hsz",
                  log_entry("2026-07-17T00:00:00.000Z", 1));
    FileServerLog malformed_logs(malformed_root);
    auto malformed_initialization = malformed_logs.initialize();
    assert(!malformed_initialization &&
           malformed_initialization.error().code == "server_log.invalid_archive_name");
    auto malformed = malformed_logs.query_all(ServerLogCategory::chat);
    assert(!malformed && malformed.error().code == "server_log.invalid_archive_name");
    std::filesystem::remove_all(malformed_root.parent_path());
}

void test_server_logs_reject_symbolic_links_and_oversized_current_files() {
    using namespace heartstead;
    using namespace heartstead::server_logs;

    const auto parent = temporary_root("log_symlink");
    const auto root = parent / "server";
    std::filesystem::create_directories(root / "logs");
    const auto outside = parent / "outside.log";
    {
        std::ofstream output(outside, std::ios::binary);
        output << ServerLogLineCodec::encode(log_entry("2026-07-17T00:00:00.000Z", 1)) << '\n';
    }
    std::error_code link_error;
    std::filesystem::create_symlink(outside, root / "logs" / "current.log", link_error);
    if (!link_error) {
        FileServerLog linked_logs(root);
        auto linked_initialization = linked_logs.initialize();
        assert(!linked_initialization &&
               linked_initialization.error().code == "server_log.unsafe_symlink");
        auto linked_query = linked_logs.query_current(ServerLogCategory::general);
        assert(!linked_query && linked_query.error().code == "server_log.unsafe_symlink");
        auto linked_append = linked_logs.append(ServerLogCategory::general,
                                                log_entry("2026-07-17T00:00:01.000Z", 2));
        assert(!linked_append && linked_append.error().code == "server_log.unsafe_symlink");
    }
    std::filesystem::remove_all(parent);

    const auto rotated_parent = temporary_root("log_rotated_symlink");
    const auto rotated_root = rotated_parent / "server";
    const auto outside_directory = rotated_parent / "outside";
    std::filesystem::create_directories(rotated_root / "logs");
    std::filesystem::create_directory(outside_directory);
    link_error.clear();
    std::filesystem::create_directory_symlink(outside_directory, rotated_root / "logs" / "rotated",
                                              link_error);
    if (!link_error) {
        FileServerLog linked_rotated_logs(rotated_root);
        auto linked_initialization = linked_rotated_logs.initialize();
        assert(!linked_initialization &&
               linked_initialization.error().code == "server_log.unsafe_symlink");
        auto linked_query = linked_rotated_logs.query_all(ServerLogCategory::general);
        assert(!linked_query && linked_query.error().code == "server_log.unsafe_symlink");
    }
    std::filesystem::remove_all(rotated_parent);

    const auto large_root = temporary_root("log_large_current") / "server";
    FileServerLog large_logs(large_root);
    assert(large_logs.append(ServerLogCategory::general, log_entry("2026-07-17T00:00:00.000Z", 1)));
    std::filesystem::resize_file(large_logs.current_path(ServerLogCategory::general),
                                 server_log_max_query_bytes + 1U);
    auto oversized_append =
        large_logs.append(ServerLogCategory::general, log_entry("2026-07-17T00:00:01.000Z", 2));
    assert(!oversized_append && oversized_append.error().code == "server_log.file_too_large");
    std::filesystem::remove_all(large_root.parent_path());
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
    test_server_log_rotation_and_archive_ordering();
    test_server_log_corruption_limits_and_filters_fail_closed();
    test_server_logs_reject_symbolic_links_and_oversized_current_files();
    test_log_sink_may_reconfigure_itself();
    return 0;
}
