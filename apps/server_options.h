#ifndef HETEROSCHED_SERVER_OPTIONS_H
#define HETEROSCHED_SERVER_OPTIONS_H

#include <charconv>
#include <cstdint>
#include <stdexcept>
#include <string_view>

struct ServerOptions {
    std::uint16_t port = 9000;
    int backlog = 1;
    bool quiet = false;
};

inline ServerOptions parseServerOptions(int argc, char* argv[]) {
    ServerOptions options;
    bool portSpecified = false;
    bool backlogSpecified = false;
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
        } else {
            throw std::invalid_argument("usage: <server> [--port 0..65535] [--backlog positive-int] [--quiet]");
        }
    }
    return options;
}

#endif
