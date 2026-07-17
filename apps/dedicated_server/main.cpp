#include "engine/content/content_validation.hpp"
#include "engine/core/process_entry.hpp"
#include "game/runtime/game_runtime.hpp"

#include <charconv>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace {

struct LaunchOptions {
    std::optional<std::uint64_t> maximum_ticks;
    bool help = false;
};

volatile std::sig_atomic_t stop_requested = 0;

extern "C" void request_stop(int) {
    stop_requested = 1;
}

heartstead::core::Result<LaunchOptions> parse_options(int argc, char** argv) {
    if (argc == 1) {
        return heartstead::core::Result<LaunchOptions>::success({});
    }
    if (argc == 2 && (std::string_view(argv[1]) == "--help" || std::string_view(argv[1]) == "-h")) {
        return heartstead::core::Result<LaunchOptions>::success(
            {.maximum_ticks = std::nullopt, .help = true});
    }
    if (argc != 3 || std::string_view(argv[1]) != "--ticks") {
        return heartstead::core::Result<LaunchOptions>::failure(
            "dedicated_server.invalid_arguments", "expected no arguments or --ticks N");
    }

    std::uint64_t ticks = 0;
    const std::string_view value(argv[2]);
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), ticks);
    if (error != std::errc{} || end != value.data() + value.size() || ticks == 0) {
        return heartstead::core::Result<LaunchOptions>::failure(
            "dedicated_server.invalid_tick_count", "--ticks must be a positive 64-bit integer");
    }
    return heartstead::core::Result<LaunchOptions>::success(
        {.maximum_ticks = ticks, .help = false});
}

void print_usage(const char* executable, std::ostream& output) {
    output << "usage: " << executable << " [--ticks N]\n"
           << "Runs until SIGINT/SIGTERM by default; --ticks provides a finite smoke run.\n";
}

int fail(const heartstead::core::Error& error) {
    std::cerr << error.code << ": " << error.message << '\n';
    return 1;
}

} // namespace

int main(int argc, char** argv) {
    return heartstead::core::run_process_entry(argv[0], [argc, argv] {
        const auto options = parse_options(argc, argv);
        if (!options) {
            print_usage(argv[0], std::cerr);
            std::cerr << options.error().code << ": " << options.error().message << '\n';
            return 2;
        }
        if (options.value().help) {
            print_usage(argv[0], std::cout);
            return 0;
        }

        if (std::signal(SIGINT, request_stop) == SIG_ERR ||
            std::signal(SIGTERM, request_stop) == SIG_ERR) {
            std::cerr
                << "dedicated_server.signal_handler_failed: failed to install shutdown handlers\n";
            return 1;
        }

        const auto content = heartstead::content::ContentValidation::validate(
            std::filesystem::path(HEARTSTEAD_SOURCE_ROOT));
        if (content.has_errors()) {
            for (const auto& diagnostic : content.diagnostics) {
                std::cerr << diagnostic.code << ": " << diagnostic.message << " ("
                          << diagnostic.source.generic_string() << ")\n";
            }
            std::cerr << "dedicated_server.content_invalid: content validation failed\n";
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

        constexpr std::uint64_t frame_time_us = 16'667;
        std::uint64_t tick_count = 0;
        std::uint64_t simulated_time_us = 0;
        auto next_frame = std::chrono::steady_clock::now();
        while (stop_requested == 0 &&
               (!options.value().maximum_ticks || tick_count < *options.value().maximum_ticks)) {
            simulated_time_us += frame_time_us;
            auto result = runtime.value().run_frame(
                {frame_time_us, static_cast<std::int64_t>(simulated_time_us / 1'000U)});
            if (!result) {
                return fail(result.error());
            }
            ++tick_count;
            if (!options.value().maximum_ticks) {
                next_frame += std::chrono::microseconds(frame_time_us);
                std::this_thread::sleep_until(next_frame);
            }
        }
        std::cout << "dedicated server: authoritative_tick="
                  << runtime.value().session()->server()->world().world_time() << " clients="
                  << runtime.value().session()->server()->host().connected_client_count() << '\n';
        status = runtime.value().shutdown();
        return status ? 0 : fail(status.error());
    });
}
