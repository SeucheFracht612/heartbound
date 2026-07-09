#include "engine/core/logging.hpp"
#include "engine/debug/inspection.hpp"
#include "engine/replay/command_replay.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace {

heartstead::core::Result<std::string> read_text_file(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        return heartstead::core::Result<std::string>::failure(
            "replay_inspector.read_failed", "failed to read command replay: " + path.string());
    }

    std::ostringstream output;
    output << input.rdbuf();
    return heartstead::core::Result<std::string>::success(output.str());
}

void print_usage(const char* executable) {
    std::cerr << "usage: " << executable << " <command_replay.txt>\n";
}

} // namespace

int main(int argc, char** argv) {
    using namespace heartstead;

    if (argc != 2) {
        print_usage(argv[0]);
        return 2;
    }

    auto text = read_text_file(argv[1]);
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
}
