#include "engine/core/filesystem.hpp"

#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

namespace {

[[nodiscard]] std::filesystem::path make_temp_root() {
    const auto parent = std::filesystem::temp_directory_path();
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    for (std::uint32_t attempt = 0; attempt < 100; ++attempt) {
        const auto root = parent / ("heartstead_replace_file_" + std::to_string(nonce) + "_" +
                                    std::to_string(attempt));
        std::error_code error;
        if (std::filesystem::create_directory(root, error)) {
            return root;
        }
    }
    assert(false && "could not create filesystem test directory");
    return {};
}

void write_text(const std::filesystem::path& path, std::string_view text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    assert(output);
    output << text;
    output.close();
    assert(output);
}

[[nodiscard]] std::string read_text(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    assert(input);
    std::ostringstream text;
    text << input.rdbuf();
    assert(!input.bad());
    return text.str();
}

void test_replace_file_installs_staged_content_and_preserves_on_failure() {
    const auto root = make_temp_root();
    const auto destination = root / "state.txt";
    const auto staged = root / "state.txt.tmp";

    write_text(destination, "old");
    write_text(staged, "new");
    assert(!heartstead::core::replace_file(staged, destination));
    assert(!std::filesystem::exists(staged));
    assert(read_text(destination) == "new");

    const auto missing = root / "missing.tmp";
    assert(heartstead::core::replace_file(missing, destination));
    assert(read_text(destination) == "new");

    const auto initially_missing = root / "created.txt";
    write_text(staged, "created");
    assert(!heartstead::core::replace_file(staged, initially_missing));
    assert(read_text(initially_missing) == "created");

    std::error_code cleanup_error;
    std::filesystem::remove_all(root, cleanup_error);
    assert(!cleanup_error);
}

} // namespace

int main() {
    test_replace_file_installs_staged_content_and_preserves_on_failure();
    return 0;
}
