#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <iostream>
#include <cerrno>
#include <optional>
#include <string>
#include <sstream>

#include "scheduler.h"
#include "worker.h"
#include "registration.h"
#include "tcp_io.h"

void handleRegistration(int clientFd, Scheduler& scheduler) {
    const auto message = receiveLine(clientFd);

    if (!message) return;

    std::cout << "Received: " << *message << std::endl;

    const auto worker = parseRegistration(*message);

    if (!worker) {
        std::cerr << "Invalid registration message" << std::endl;

        if (!sendAll(clientFd, "REGISTER_ERROR INVALID_MESSAGE\n")) return;

        return;
    }

    if (!scheduler.addWorker(*worker)) {
        std::cerr << "Worker registration rejected" << std::endl;

        if (!sendAll(clientFd, "REGISTER_ERROR WORKER_REJECTED\n")) return;

        return;
    }

    if (!sendAll(clientFd, "REGISTER_OK\n")) return;

    std::cout << "Registered Worker " << worker->getId()
        << ", worker count: " << scheduler.workerCount()
        << std::endl;
}

int main() {
    Scheduler scheduler;

    const int serverFd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd == -1) {
        std::perror("socket failed");
        return 1;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(9000);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    // 调用 bind()，失败时打印错误、关闭 serverFd 并返回 1。
    if (bind(serverFd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) < 0) {
        std::perror("bind failed");
        close(serverFd);
        return 1;
    }

    // 调用 listen()，backlog 使用 1。
    // 失败时同样处理错误并关闭 serverFd。
    if (listen(serverFd, 1) < 0) {
        std::perror("listen failed");
        close(serverFd);
        return 1;
    }

    // listen 成功后，输出：
    // Listening on 127.0.0.1:9000
    // 使用 std::endl，确保进入阻塞等待前日志已经输出。
    std::cout << "Listening on 127.0.0.1:9000" << std::endl;

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

        std::cout << "Client connected" << std::endl;

        handleRegistration(clientFd, scheduler);
        close(clientFd);
    }

    close(serverFd);
    return 0;
}