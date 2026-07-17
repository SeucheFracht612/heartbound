#include "engine/core/filesystem.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace heartstead::core {

std::error_code replace_file(const std::filesystem::path& staged,
                             const std::filesystem::path& destination) noexcept {
#ifdef _WIN32
    if (::MoveFileExW(staged.c_str(), destination.c_str(),
                      MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0) {
        return {static_cast<int>(::GetLastError()), std::system_category()};
    }
    return {};
#else
    std::error_code error;
    std::filesystem::rename(staged, destination, error);
    return error;
#endif
}

} // namespace heartstead::core
