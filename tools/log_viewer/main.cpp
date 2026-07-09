#include "engine/server_logs/server_log.hpp"

#include <filesystem>
#include <iostream>
#include <optional>
#include <string_view>

namespace {

[[nodiscard]] std::optional<heartstead::server_logs::ServerLogCategory>
parse_category(std::string_view value) {
    if (value == "chat") {
        return heartstead::server_logs::ServerLogCategory::chat;
    }
    if (value == "audit") {
        return heartstead::server_logs::ServerLogCategory::audit;
    }
    if (value == "server") {
        return heartstead::server_logs::ServerLogCategory::general;
    }
    return std::nullopt;
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        std::cout << "usage: heartstead_log_viewer <server-root> [server|chat|audit] "
                     "[event-type] [message-substring]\n";
        return 0;
    }
    if (argc < 2) {
        std::cerr << "usage: heartstead_log_viewer <server-root> [server|chat|audit] "
                     "[event-type] [message-substring]\n";
        return 2;
    }

    const auto category = argc >= 3
                              ? parse_category(argv[2])
                              : std::optional{heartstead::server_logs::ServerLogCategory::general};
    if (!category) {
        std::cerr << "category must be server, chat, or audit\n";
        return 2;
    }
    heartstead::server_logs::ServerLogFilter filter;
    if (argc >= 4) {
        filter.event_type = argv[3];
    }
    if (argc >= 5) {
        filter.message_contains = argv[4];
    }

    heartstead::server_logs::FileServerLog logs{std::filesystem::path(argv[1])};
    auto entries = logs.query_current(*category, filter);
    if (!entries) {
        std::cerr << entries.error().code << ": " << entries.error().message << '\n';
        return 1;
    }
    for (const auto& entry : entries.value()) {
        std::cout << heartstead::server_logs::ServerLogLineCodec::encode(entry) << '\n';
    }
    return 0;
}
