#include "engine/content/content_validation.hpp"
#include "game/runtime/game_runtime.hpp"

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string_view>

namespace {

std::uint32_t parse_tick_count(int argc, char** argv) {
    if (argc != 3 || std::string_view(argv[1]) != "--ticks") {
        return 120;
    }
    std::uint32_t ticks = 0;
    const auto value = std::string_view(argv[2]);
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), ticks);
    return error == std::errc{} && end == value.data() + value.size() && ticks > 0 ? ticks : 120;
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
        content, "dedicated-server", 0x4845415254535445ULL);
    if (!metadata) {
        return fail(metadata.error());
    }

    heartstead::game::RuntimeConfiguration config;
    config.create_server = true;
    config.create_client = false;
    config.headless = true;
    heartstead::game::SessionRequest request;
    request.metadata = std::move(metadata).value();
    auto status = runtime.value().start_session(config, std::move(request));
    if (!status) {
        return fail(status.error());
    }

    const auto tick_count = parse_tick_count(argc, argv);
    for (std::uint32_t tick = 0; tick < tick_count; ++tick) {
        auto result = runtime.value().run_frame(
            {16'667, static_cast<std::int64_t>((tick + 1U) * 16U)});
        if (!result) {
            return fail(result.error());
        }
    }
    std::cout << "dedicated server: authoritative_tick="
              << runtime.value().session()->server()->world().world_time()
              << " clients="
              << runtime.value().session()->server()->host().connected_client_count() << '\n';
    status = runtime.value().shutdown();
    return status ? 0 : fail(status.error());
}
