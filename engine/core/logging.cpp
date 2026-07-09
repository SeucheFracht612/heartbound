#include "engine/core/logging.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <mutex>

namespace heartstead::core {

namespace {

std::mutex g_log_mutex;
std::mutex g_default_sink_mutex;
LogSink g_sink;

void default_sink(LogLevel level, std::string_view message) {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);

    std::tm local_time{};
#if defined(_WIN32)
    localtime_s(&local_time, &time);
#else
    localtime_r(&time, &local_time);
#endif

    auto& stream = level == LogLevel::error || level == LogLevel::fatal ? std::cerr : std::cout;
    stream << std::put_time(&local_time, "%H:%M:%S") << " [" << log_level_name(level) << "] "
           << message << '\n';
}

} // namespace

void set_log_sink(LogSink sink) {
    std::scoped_lock lock(g_log_mutex);
    g_sink = std::move(sink);
}

void reset_log_sink() {
    std::scoped_lock lock(g_log_mutex);
    g_sink = {};
}

void log(LogLevel level, std::string_view message) {
    LogSink sink;
    {
        std::scoped_lock lock(g_log_mutex);
        sink = g_sink;
    }
    if (sink) {
        sink(level, message);
    } else {
        std::scoped_lock lock(g_default_sink_mutex);
        default_sink(level, message);
    }
}

std::string_view log_level_name(LogLevel level) noexcept {
    switch (level) {
    case LogLevel::trace:
        return "trace";
    case LogLevel::debug:
        return "debug";
    case LogLevel::info:
        return "info";
    case LogLevel::warning:
        return "warning";
    case LogLevel::error:
        return "error";
    case LogLevel::fatal:
        return "fatal";
    }
    return "unknown";
}

} // namespace heartstead::core
