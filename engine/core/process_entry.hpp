#pragma once

#include "engine/core/logging.hpp"

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <functional>
#include <iostream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace heartstead::core {

namespace detail {

inline void report_process_failure(std::string_view executable, std::string_view message) noexcept {
    try {
        log(LogLevel::fatal, std::string(executable) + ": " + std::string(message));
    } catch (...) {
        // Diagnostics must not turn an already-failing process boundary into std::terminate.
        std::fputs("heartstead: fatal process error\n", stderr);
        std::fflush(stderr);
    }
}

} // namespace detail

template <typename Callable>
[[nodiscard]] int run_process_entry(std::string_view executable, Callable&& callable) noexcept {
    static_assert(std::is_invocable_r_v<int, Callable>,
                  "a guarded process entry point must return an int exit code");
    try {
        const auto exit_code = std::invoke(std::forward<Callable>(callable));
        std::cout.flush();
        std::cerr.flush();
        if (!std::cout || !std::cerr) {
            detail::report_process_failure(executable, "failed to flush process output");
            return EXIT_FAILURE;
        }
        return exit_code;
    } catch (const std::exception& exception) {
        detail::report_process_failure(executable,
                                       std::string("unhandled exception: ") + exception.what());
    } catch (...) {
        detail::report_process_failure(executable, "unhandled non-standard exception");
    }
    return EXIT_FAILURE;
}

} // namespace heartstead::core
