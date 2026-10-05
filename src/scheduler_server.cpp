#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <iostream>
#include <cerrno>
#include <optional>
#include <string>

std::optional<std::string> receiveLine(int fd) {
    char ch;
    std::string line;

    while (true) {
        const ssize_t count = recv(fd, &ch, 1, 0);
        
        if (count == -1) {
            if (errno == EINTR) {
                continue;
            }

            perror("recv failed");
            return std::nullopt;
        }

        if (count == 0) {
            std::cerr << "Incomplete message" << std::endl;
            return std::nullopt;
        }

        if (ch == '\n') {
            return line;
        }

        if (line.size() == 128) {
            std::cerr << "message too long" << std::endl;
            return std::nullopt;
        }

        line += ch;
    }
}

int main() {
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

    // 调用 accept()，接收一个连接。
    // 成功时输出 Client connected，并关闭返回的连接描述符。
    // 失败时打印错误、关闭 serverFd 并返回 1。
    const int clientFd = accept(serverFd, nullptr, nullptr);
    if (clientFd < 0) {
        std::perror("accept failed");
        close(serverFd);
        return 1;
    }

    std::cout << "Client connected" << std::endl;

    const auto message = receiveLine(clientFd);

    if (!message) {
        close(clientFd);
        close(serverFd);
        return 1;
    }

    std::cout << "Received: " << *message << std::endl;

    close(clientFd);
    close(serverFd);
    return 0;
}