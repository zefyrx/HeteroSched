#include <iostream>

#include "core/scheduler.h"
#include "net/epoll_server.h"
#include "protocol/scheduler_protocol.h"
#include "server_options.h"
#include "server_logging.h"

int main(int argc, char* argv[]) {
    try {
        const auto options = parseServerOptions(argc, argv, true);

        Scheduler scheduler;
        EpollServer server(options.port, [&](const std::string& line) {
            const auto response = processSchedulerRequest(line, scheduler);
            if (!options.quiet) logSchedulerRequest(line, response, scheduler);
            return response;
        }, options.backlog, std::chrono::milliseconds{options.idleTimeoutMs});
        std::cout << "Listening on 127.0.0.1:" << server.port() << std::endl;
        server.run();
    } catch (const std::exception& error) {
        std::cerr << "scheduler_server_epoll: " << error.what() << '\n';
        return 1;
    }
}
