#include "engine/core/file_io.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>

int main() {
    const auto path = std::filesystem::temp_directory_path() / "heartstead_file_io_tests.txt";
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << "test";
        assert(output);
    }

    const auto loaded = heartstead::core::read_text_file(path);
    assert(loaded);
    assert(loaded.value() == "test");

    const auto bounded = heartstead::core::read_text_file(path, {.maximum_bytes = 3});
    assert(!bounded);
    assert(bounded.error().code == "core.file_too_large");

    std::error_code error;
    std::filesystem::remove(path, error);
    assert(!error);
    return 0;
}
