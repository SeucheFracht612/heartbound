#pragma once

namespace heartstead::core {

[[noreturn]] void assertion_failed(const char* expression, const char* file, int line,
                                   const char* message);

} // namespace heartstead::core

#define HEARTSTEAD_ASSERT(expression, message)                                                     \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            ::heartstead::core::assertion_failed(#expression, __FILE__, __LINE__, (message));      \
        }                                                                                          \
    } while (false)
