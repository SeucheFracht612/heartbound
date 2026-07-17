#include "engine/core/file_io.hpp"
#include "engine/core/logging.hpp"
#include "engine/core/process_entry.hpp"
#include "engine/debug/inspection.hpp"
#include "engine/replay/command_replay.hpp"

#include <filesystem>
#include <iostream>
#include <string_view>

namespace {

void print_usage(const char* executable, std::ostream& output) {
    output << "usage: " << executable << " <command_replay.txt>\n";
}

} // namespace

int main(int argc, char** argv) {
    return heartstead::core::run_process_entry(argv[0], [argc, argv] {
        using namespace heartstead;

        if (argc == 2 &&
            (std::string_view(argv[1]) == "--help" || std::string_view(argv[1]) == "-h")) {
            print_usage(argv[0], std::cout);
            return 0;
        }
        if (argc != 2) {
            print_usage(argv[0], std::cerr);
            return 2;
        }

        auto text = core::read_text_file(argv[1]);
        if (!text) {
            core::log(core::LogLevel::error, text.error().message);
            return 1;
        }

        auto log = replay::CommandReplayCodec::decode(text.value());
        if (!log) {
            core::log(core::LogLevel::error, log.error().message);
            return 1;
        }

        const auto inspection = debug::Inspector::inspect(log.value());
        std::cout << debug::Inspector::render_text(inspection);
        return inspection.has_errors() ? 1 : 0;
    });
}
