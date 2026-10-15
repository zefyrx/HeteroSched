#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>

#include "protocol/scheduler_protocol.h"
#include "net/tcp_io.h"

int main(int argc, char* argv[]) {
    std::string request;
    bool scheduleRequest = false;
    if (argc == 2 && std::string{argv[1]} == "next") {
        request = "SCHEDULE_NEXT\n";
        scheduleRequest = true;
    } else if (argc == 3 && std::string{argv[1]} == "complete") {
        // Job ID 文本交给服务端校验。
        request = std::string{"COMPLETE_JOB "} + argv[2] + "\n";
    } else {
        std::cerr << "Usage: scheduler_client next\n"
                  << "       scheduler_client complete <jobId>\n";
        return 1;
    }

    const int clientFd = socket(AF_INET, SOCK_STREAM, 0);
    if (clientFd == -1) {
        std::perror("socket failed");
        return 1;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(9000);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(clientFd, reinterpret_cast<const sockaddr*>(&address),
                sizeof(address)) == -1) {
        std::perror("connect failed");
        close(clientFd);
        return 1;
    }

    if (!sendAll(clientFd, request)) {
        close(clientFd);
        return 1;
    }
    const auto message = receiveLine(clientFd);
    close(clientFd);
    if (!message) {
        return 1;
    }

    if (scheduleRequest) {
        if (const auto result = parseScheduleResult(*message)) {
            std::cout << "Scheduled Job " << result->jobId
                      << " on Worker " << result->workerId << '\n';
            return 0;
        }
        if (*message == "SCHEDULE_NONE") {
            std::cout << "No schedulable Job\n";
            return 0;
        }
    } else if (*message == "COMPLETE_OK") {
        std::cout << "Job completion accepted\n";
        return 0;
    }

    std::istringstream input(*message);
    std::string command;
    std::string error;
    std::string extra;
    const std::string expectedError =
        scheduleRequest ? "SCHEDULE_ERROR" : "COMPLETE_ERROR";
    if ((input >> command >> error) && command == expectedError &&
        !(input >> extra)) {
        std::cerr << (scheduleRequest ? "Scheduling failed: " : "Completion failed: ")
                  << error << '\n';
    } else {
        std::cerr << "Unexpected "
                  << (scheduleRequest ? "scheduling" : "completion")
                  << " response: " << *message << '\n';
    }
    return 1;
}
