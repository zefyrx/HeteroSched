#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/time.h>

#include <cstdio>
#include <iostream>
#include <cerrno>
#include <string>

#include "core/scheduler.h"
#include "protocol/scheduler_protocol.h"
#include "net/tcp_io.h"
#include "server_options.h"
#include "server_logging.h"

void handleClient(int clientFd, Scheduler& scheduler, bool quiet) {
    const auto message = receiveLine(clientFd, !quiet);
    if (!message) {
        return;
    }

    const std::string response = processSchedulerRequest(*message, scheduler);
    if (!quiet) logSchedulerRequest(*message, response, scheduler);
    if (!sendAll(clientFd, response, !quiet)) {
        return;
    }
}

int main(int argc, char* argv[]) {
    ServerOptions options;
    try {
        options = parseServerOptions(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << "scheduler_server: " << error.what() << '\n';
        return 1;
    }
    Scheduler scheduler;

    const int serverFd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd == -1) {
        std::perror("socket failed");
        return 1;
    }

    const int reuseAddress = 1;
    if (setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &reuseAddress, sizeof(reuseAddress)) < 0) {
        std::perror("setsockopt SO_REUSEADDR failed");
        close(serverFd);
        return 1;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(options.port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    // 调用 bind()，失败时打印错误、关闭 serverFd 并返回 1。
    if (bind(serverFd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) < 0) {
        std::perror("bind failed");
        close(serverFd);
        return 1;
    }

    // 默认 backlog 为 1，可通过参数独立进行监听队列实验。
    // 失败时同样处理错误并关闭 serverFd。
    if (listen(serverFd, options.backlog) < 0) {
        std::perror("listen failed");
        close(serverFd);
        return 1;
    }

    // listen 成功后，输出：
    // Listening on 127.0.0.1:9000
    // 使用 std::endl，确保进入阻塞等待前日志已经输出。
    socklen_t addressLength = sizeof(address);
    if (getsockname(serverFd, reinterpret_cast<sockaddr*>(&address), &addressLength) < 0) {
        std::perror("getsockname failed");
        close(serverFd);
        return 1;
    }
    std::cout << "Listening on 127.0.0.1:" << ntohs(address.sin_port) << std::endl;

    while (true) {
        const int clientFd = accept(serverFd, nullptr, nullptr);

        if (clientFd < 0) {
            if (errno == EINTR) {
                continue;
            }

            std::perror("accept failed");
            close(serverFd);
            return 1;
        }

        if (!options.quiet) std::cout << "Client connected" << std::endl;

        timeval timeout{};
        timeout.tv_sec = 5;
        timeout.tv_usec = 0;

        if (setsockopt(clientFd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == -1) {
            std::perror("setsockopt failed");
            // 关闭当前 clientFd。
            close(clientFd);
            // continue，等待下一个连接。
            continue;
        }

        handleClient(clientFd, scheduler, options.quiet);
        if (!options.quiet) std::cout << std::endl;

        close(clientFd);
    }

}
