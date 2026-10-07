#include <sys/socket.h>
#include <iostream>
#include <cerrno>
#include <cstdio>

#include "tcp_io.h"

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