#include "engine/core/logging.hpp"
#include "engine/debug/inspection.hpp"
#include "engine/workpieces/workpiece_codec.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace {

heartstead::core::Result<std::string> read_text_file(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        return heartstead::core::Result<std::string>::failure(
            "workpiece_inspector.read_failed", "failed to read workpiece grid: " + path.string());
    }

    std::ostringstream output;
    output << input.rdbuf();
    return heartstead::core::Result<std::string>::success(output.str());
}

void print_usage(const char* executable) {
    std::cerr << "usage: " << executable << " <workpiece_grid.txt>\n";
}

} // namespace

int main(int argc, char** argv) {
    using namespace heartstead;

    if (argc < 2) {
        print_usage(argv[0]);
        return 2;
    }

    auto text = read_text_file(argv[1]);
    if (!text) {
        core::log(core::LogLevel::error, text.error().message);
        return 1;
    }

    auto grid = workpieces::WorkpieceGridTextCodec::decode(text.value());
    if (!grid) {
        core::log(core::LogLevel::error, grid.error().message);
        return 1;
    }

    std::cout << debug::Inspector::render_text(debug::Inspector::inspect(grid.value()));
    return 0;
}
