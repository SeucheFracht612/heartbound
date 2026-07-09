#include "engine/core/assert.hpp"

#include "engine/core/logging.hpp"

#include <cstdlib>
#include <string>

namespace heartstead::core {

void assertion_failed(const char* expression, const char* file, int line, const char* message) {
    log(LogLevel::fatal, std::string("assertion failed: ") + expression + " at " + file + ":" +
                             std::to_string(line) + " - " + message);
    std::abort();
}

} // namespace heartstead::core
