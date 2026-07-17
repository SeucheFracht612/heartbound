#include "engine/content/content_validation.hpp"
#include "game/runtime/game_runtime.hpp"

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string_view>

namespace {

std::uint32_t parse_frame_count(int argc, char** argv) {
    if (argc != 3 || std::string_view(argv[1]) != "--frames") {
        return 120;
    }
    std::uint32_t frames = 0;
    const auto value = std::string_view(argv[2]);
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), frames);
    return error == std::errc{} && end == value.data() + value.size() && frames > 0 ? frames : 120;
}

int fail(const heartstead::core::Error& error) {
    std::cerr << error.code << ": " << error.message << '\n';
    return 1;
}

} // namespace

int main(int argc, char** argv) {
    const auto content = heartstead::content::ContentValidation::validate(
        std::filesystem::path(HEARTSTEAD_SOURCE_ROOT));
    if (content.has_errors()) {
        std::cerr << "content validation failed\n";
        return 1;
    }
    auto runtime = heartstead::game::GameRuntime::initialize(
        heartstead::game::GameRuntimeConfig{}, content);
    if (!runtime) {
        return fail(runtime.error());
    }
    auto metadata = heartstead::content::save_metadata_from_content_report(
        content, "development", 0x4845415254535445ULL);
    if (!metadata) {
        return fail(metadata.error());
    }

    heartstead::game::RuntimeConfiguration config;
    config.create_server = true;
    config.create_client = true;
    config.use_in_memory_transport = true;
    config.headless = true;
    heartstead::game::SessionRequest request;
    request.metadata = std::move(metadata).value();
    auto status = runtime.value().start_session(config, std::move(request));
    if (!status) {
        return fail(status.error());
    }

    const auto frame_count = parse_frame_count(argc, argv);
    std::uint64_t simulated_us = 0;
    heartstead::game::RuntimeFrameStats last_frame;
    for (std::uint32_t frame = 0; frame < frame_count; ++frame) {
        simulated_us += 16'667;
        auto result = runtime.value().run_frame(
            {16'667, static_cast<std::int64_t>(simulated_us / 1000U)});
        if (!result) {
            return fail(result.error());
        }
        last_frame = std::move(result).value();
    }

    std::cout << "development runtime: frames=" << frame_count
              << " authoritative_tick=" << last_frame.authoritative_world_tick
              << " local_client="
              << (runtime.value().session()->client()->is_connected() ? "connected" : "offline")
              << '\n';
    status = runtime.value().shutdown();
    return status ? 0 : fail(status.error());
}
