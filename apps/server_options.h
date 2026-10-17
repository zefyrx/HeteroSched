#ifndef HETEROSCHED_SERVER_OPTIONS_H
#define HETEROSCHED_SERVER_OPTIONS_H

#include <charconv>
#include <cstdint>
#include <stdexcept>
#include <string_view>

struct ServerOptions {
    std::uint16_t port = 9000;
    int backlog = 1;
    int idleTimeoutMs = 0;
    int maxConnections = 0;
    bool quiet = false;
};

inline ServerOptions parseServerOptions(int argc, char* argv[], bool allowEpollOptions = false) {
    ServerOptions options;
    bool portSpecified = false;
    bool backlogSpecified = false;
    bool idleTimeoutSpecified = false;
    bool maxConnectionsSpecified = false;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--quiet") {
            options.quiet = true;
        } else if (argument == "--port" && index + 1 < argc && !portSpecified) {
            const std::string_view value(argv[++index]);
            unsigned int port = 0;
            const auto result = std::from_chars(value.data(), value.data() + value.size(), port);
            if (result.ec != std::errc{} || result.ptr != value.data() + value.size() || port > 65535)
                throw std::invalid_argument("--port must be an integer between 0 and 65535");
            options.port = static_cast<std::uint16_t>(port);
            portSpecified = true;
        } else if (argument == "--backlog" && index + 1 < argc && !backlogSpecified) {
            const std::string_view value(argv[++index]);
            const auto result = std::from_chars(value.data(), value.data() + value.size(), options.backlog);
            if (result.ec != std::errc{} || result.ptr != value.data() + value.size() || options.backlog <= 0)
                throw std::invalid_argument("--backlog must be a positive int");
            backlogSpecified = true;
        } else if (argument == "--idle-timeout-ms" && allowEpollOptions &&
                   index + 1 < argc && !idleTimeoutSpecified) {
            const std::string_view value(argv[++index]);
            const auto result = std::from_chars(value.data(), value.data() + value.size(), options.idleTimeoutMs);
            if (result.ec != std::errc{} || result.ptr != value.data() + value.size() || options.idleTimeoutMs < 0)
                throw std::invalid_argument("--idle-timeout-ms must be a nonnegative int (0 disables it)");
            idleTimeoutSpecified = true;
        } else if (argument == "--max-connections" && allowEpollOptions &&
                   index + 1 < argc && !maxConnectionsSpecified) {
            const std::string_view value(argv[++index]);
            const auto result = std::from_chars(value.data(), value.data() + value.size(), options.maxConnections);
            if (result.ec != std::errc{} || result.ptr != value.data() + value.size() || options.maxConnections < 0)
                throw std::invalid_argument("--max-connections must be a nonnegative int (0 disables it)");
            maxConnectionsSpecified = true;
        } else {
            throw std::invalid_argument(allowEpollOptions
                ? "usage: <server> [--port 0..65535] [--backlog positive-int] [--idle-timeout-ms 0..2147483647] [--max-connections 0..2147483647] [--quiet]"
                : "usage: <server> [--port 0..65535] [--backlog positive-int] [--quiet]");
        }
    }
    return options;
}

#endif
