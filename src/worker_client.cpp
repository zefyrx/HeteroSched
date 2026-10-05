#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <iostream>
#include <cerrno>
#include <string>

bool sendAll(int fd, const std::string& message) {
    std::size_t sentBytes = 0;

    while (sentBytes < message.size()) {
        const ssize_t count = send(
            fd,
            message.data() + sentBytes,
            message.size() - sentBytes,
            MSG_NOSIGNAL
        );

        // count == -1 且 errno == EINTR：continue，重试。
        // 其他发送错误：perror("send failed")，返回 false。
        if (count == -1) {
            if (errno == EINTR) {
                continue;
            }

            perror("send failed");
            return false;
        }

        // count == 0：打印无法继续发送的错误，返回 false。
        if (count == 0) {
            std::cerr << "unable to continue send" << std::endl;
            return false;
        }

        // count > 0：转换为 std::size_t，再累加到 sentBytes。
        if (count > 0) {
            sentBytes += static_cast<std::size_t>(count);
        }
    }

    return true;
}

int main() {
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

    const std::string message = "REGISTER_WORKER 1 8 16384\n";

    if (!sendAll(clientFd, message)) {
        close(clientFd);
        return 1;
    }

    std::cout << "Registration message sent" << std::endl;

    close(clientFd);
    return 0;
}