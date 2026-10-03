#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <iostream>

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

    std::cout << "Connected to Scheduler at 127.0.0.1:9000"
              << std::endl;

    close(clientFd);
    return 0;
}