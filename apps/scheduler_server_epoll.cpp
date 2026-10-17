#include <charconv>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string_view>

#include "core/scheduler.h"
#include "net/epoll_server.h"
#include "protocol/scheduler_protocol.h"

int main(int argc, char* argv[]) {
    try {
        unsigned int port = 9000;
        if (argc != 1) {
            if (argc != 3 || std::string_view(argv[1]) != "--port")
                throw std::invalid_argument("usage: scheduler_server_epoll [--port 0..65535]");
            const std::string_view value(argv[2]);
            const auto result = std::from_chars(value.data(), value.data() + value.size(), port);
            if (result.ec != std::errc{} || result.ptr != value.data() + value.size() || port > 65535)
                throw std::invalid_argument("--port must be an integer between 0 and 65535");
        }

        Scheduler scheduler;
        EpollServer server(static_cast<std::uint16_t>(port), [&](const std::string& line) {
            return processSchedulerRequest(line, scheduler);
        });
        std::cout << "Listening on 127.0.0.1:" << server.port() << std::endl;
        server.run();
    } catch (const std::exception& error) {
        std::cerr << "scheduler_server_epoll: " << error.what() << '\n';
        return 1;
    }
}
