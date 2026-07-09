#pragma once

#include <functional>
#include <string_view>

namespace heartstead::core {

enum class LogLevel {
    trace,
    debug,
    info,
    warning,
    error,
    fatal,
};

using LogSink = std::function<void(LogLevel level, std::string_view message)>;

void set_log_sink(LogSink sink);
void reset_log_sink();
void log(LogLevel level, std::string_view message);

[[nodiscard]] std::string_view log_level_name(LogLevel level) noexcept;

} // namespace heartstead::core
