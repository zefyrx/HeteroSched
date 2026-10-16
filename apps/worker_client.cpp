#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <thread>

#include "net/tcp_io.h"
#include "protocol/scheduler_protocol.h"

namespace {

// 每个请求独立连接，返回前关闭；本地执行和轮询等待不占用服务端连接。
std::optional<std::string> requestScheduler(const std::string& request) {
    const int clientFd = socket(AF_INET, SOCK_STREAM, 0);
    if (clientFd == -1) {
        std::perror("socket failed");
        return std::nullopt;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(9000);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(clientFd, reinterpret_cast<const sockaddr*>(&address),
                sizeof(address)) == -1) {
        std::perror("connect failed");
        close(clientFd);
        return std::nullopt;
    }

    if (!sendAll(clientFd, request)) {
        close(clientFd);
        return std::nullopt;
    }
    if (request.rfind("REGISTER_WORKER ", 0) == 0) {
        std::cout << "Connected to Scheduler at 127.0.0.1:9000" << std::endl;
        std::cout << "Registration message sent" << std::endl;
    }

    const auto response = receiveLine(clientFd);
    close(clientFd);
    return response;
}

void reportFailure(const std::string& response, const std::string& errorCommand,
                   const std::string& operation) {
    std::istringstream input(response);
    std::string command;
    std::string error;
    std::string extra;
    if ((input >> command >> error) && command == errorCommand && !(input >> extra)) {
        std::cerr << operation << " failed: " << error << std::endl;
    } else {
        std::cerr << "Unexpected " << operation << " response: " << response << std::endl;
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    // 只检查参数数量，注册内容和后续 FETCH_JOB 的 ID 仍由服务端校验。
    if (argc != 4) {
        std::cerr << "Usage: worker_client <workerId> <cpuCores> <memoryMB>" << std::endl;
        return 1;
    }

    const std::string registration =
        std::string{"REGISTER_WORKER "} + argv[1] + " " +
        argv[2] + " " + argv[3] + "\n";
    const auto response = requestScheduler(registration);
    if (!response) {
        return 1;
    }
    if (*response != "REGISTER_OK") {
        reportFailure(*response, "REGISTER_ERROR", "Registration");
        return 1;
    }
    std::cout << "Registration success" << std::endl;

    const std::string fetchRequest = std::string{"FETCH_JOB "} + argv[1] + "\n";
    while (true) {
        const auto assignment = requestScheduler(fetchRequest);
        if (!assignment) {
            std::cerr << "Fetch response unavailable; stopping Worker" << std::endl;
            return 1;
        }
        if (*assignment == "NO_JOB") {
            std::cout << "No Job available; retrying in 1s" << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        const auto job = parseRunJob(*assignment);
        if (!job) {
            reportFailure(*assignment, "FETCH_ERROR", "Fetch");
            return 1;
        }

        std::cout << "Worker " << argv[1] << " started Job " << job->jobId
                  << " (CPU=" << job->requiredResource.cpuCores
                  << ", memoryMB=" << job->requiredResource.memoryMB << ")" << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        std::cout << "Worker " << argv[1] << " finished Job " << job->jobId << std::endl;

        const auto completion = requestScheduler(
            "COMPLETE_JOB " + std::to_string(job->jobId) + "\n");
        if (!completion) {
            std::cerr << "Completion response unavailable; stopping Worker" << std::endl;
            return 1;
        }
        if (*completion != "COMPLETE_OK") {
            reportFailure(*completion, "COMPLETE_ERROR", "Completion");
            return 1;
        }
        std::cout << "Job " << job->jobId << " completion accepted" << std::endl;
    }
}
