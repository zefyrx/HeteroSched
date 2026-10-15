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

void printSchedulerState(const Scheduler& scheduler) {
    std::cout << "State: workers=" << scheduler.workerCount()
              << ", waiting=" << scheduler.waitingJobCount()
              << ", running=" << scheduler.runningJobCount() << std::endl;
    for (const auto& worker : scheduler.getWorkerSnapshots()) {
        std::cout << "Worker " << worker.workerId
                  << ": available CPU=" << worker.availableResource.cpuCores
                  << ", memoryMB=" << worker.availableResource.memoryMB
                  << std::endl;
    }
}

void handleClient(int clientFd, Scheduler& scheduler) {
    const auto message = receiveLine(clientFd);
    if (!message) {
        return;
    }

    std::cout << "Received: " << *message << std::endl;
    const std::string response = processSchedulerRequest(*message, scheduler);
    std::cout << "Response: " << response << std::flush;
    printSchedulerState(scheduler);
    if (!sendAll(clientFd, response)) {
        return;
    }
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

        handleClient(clientFd, scheduler);
        std::cout << std::endl;

        close(clientFd);
    }

}
