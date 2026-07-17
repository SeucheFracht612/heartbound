#pragma once

#include <filesystem>
#include <system_error>

namespace heartstead::core {

// Replaces destination with a fully written staged file. The staged file must be on the same
// filesystem as the destination. Failure leaves an existing destination in place.
[[nodiscard]] std::error_code replace_file(const std::filesystem::path& staged,
                                           const std::filesystem::path& destination) noexcept;

} // namespace heartstead::core
