#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <iostream>
#include <cerrno>
#include <string>

#include "tcp_io.h"

int main(int argc, char* argv[]) {
    // 只检查启动参数是否齐全，提交内容由服务端解析和校验。
    if (argc != 4) {
        std::cerr << "Usage: job_client <jobId> <cpuCores> <memoryMB>" << std::endl;
        return 1;
    }

    const int clientFd = socket(AF_INET, SOCK_STREAM, 0);
    if (clientFd == -1) {
        std::perror("socket failed");
        return 1;
    }

    sockaddr_in serverAddress{};
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(9000);

    // 使用 inet_pton() 将 "127.0.0.1"
    // 转换到 serverAddress.sin_addr。
    // 转换失败时打印错误、关闭 clientFd，并返回 1。
    switch (inet_pton(AF_INET, "127.0.0.1", &serverAddress.sin_addr)) {
        case 0:
            std::cerr << "address invalid" << std::endl;
            close(clientFd);
            return 1;
        case -1:
            std::perror("inet_pton failed");
            close(clientFd);
            return 1;
    }

    // 调用 connect()。
    // 失败时使用 perror() 输出错误、关闭 clientFd，并返回 1。
    if (connect(clientFd, reinterpret_cast<const sockaddr*>(&serverAddress), sizeof(serverAddress)) == -1) {
        std::perror("connect failed");
        close(clientFd);
        return 1;
    }

    std::cout << "Connected to Scheduler at 127.0.0.1:9000" << std::endl;

    const std::string request =
        std::string{"SUBMIT_JOB "} + argv[1] + " " +
        argv[2] + " " + argv[3] + "\n";

    if (!sendAll(clientFd, request)) {
        close(clientFd);
        return 1;
    }

    std::cout << "Submit message sent" << std::endl;

    const auto message = receiveLine(clientFd);

    if (!message) {
        close(clientFd);
        return 1;
    }

    const std::string errorPrefix = "SUBMIT_ERROR ";

    if (*message == "SUBMIT_OK") {
        std::cout << "Submit success" << std::endl;
    } else if (message->rfind(errorPrefix, 0) == 0) {
        std::cerr << "Submit failed: " << message->substr(errorPrefix.size()) << std::endl;
        close(clientFd);
        return 1;
    } else {
        std::cerr << "Unexpected submit response" << std::endl;
        close(clientFd);
        return 1;
    }

    close(clientFd);
    return 0;
}
