#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <iostream>
#include <string>

#include "net/tcp_io.h"

namespace {

// 每个测试拥有一对 socket，退出任何分支时都自动关闭。
class SocketPair {
public:
    SocketPair() = default;
    SocketPair(const SocketPair&) = delete;
    SocketPair& operator=(const SocketPair&) = delete;

    ~SocketPair() {
        for (int fd : fds_) {
            if (fd >= 0) {
                close(fd);
            }
        }
    }

    bool open() {
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds_) == -1) {
            std::perror("socketpair failed");
            return false;
        }
        return true;
    }

    int sender() const { return fds_[0]; }
    int receiver() const { return fds_[1]; }

    void closeSender() {
        close(fds_[0]);
        fds_[0] = -1;
    }

private:
    int fds_[2]{-1, -1};
};

bool expectCompleteLine(const std::string& payload,
                        const std::string& expected) {
    SocketPair sockets;
    if (!sockets.open()) {
        return false;
    }

    if (!sendAll(sockets.sender(), payload)) {
        std::cerr << "FAIL: could not send complete line\n";
        return false;
    }

    const auto line = receiveLine(sockets.receiver());
    if (!line || *line != expected) {
        std::cerr << "FAIL: received line does not match expected content\n";
        return false;
    }

    return true;
}

bool testCompleteLine() {
    return expectCompleteLine("HELLO\n", "HELLO");
}

bool testEmptyLine() {
    // 空行必须得到有值的空字符串，而不是 std::nullopt。
    return expectCompleteLine("\n", "");
}

bool testMaximumLengthLine() {
    const std::string content(128, 'x');
    return expectCompleteLine(content + "\n", content);
}

bool testRejectsOverlongLine() {
    SocketPair sockets;
    if (!sockets.open()) {
        return false;
    }

    if (!sendAll(sockets.sender(), std::string(129, 'x') + "\n")) {
        std::cerr << "FAIL: could not send overlong line\n";
        return false;
    }

    if (receiveLine(sockets.receiver())) {
        std::cerr << "FAIL: overlong line was accepted\n";
        return false;
    }

    return true;
}

bool testRejectsIncompleteLine() {
    SocketPair sockets;
    if (!sockets.open()) {
        return false;
    }

    if (!sendAll(sockets.sender(), "HELLO")) {
        std::cerr << "FAIL: could not send incomplete line\n";
        return false;
    }

    // 发送端关闭后，接收端读完已有字节，再从 recv() 得到 EOF。
    sockets.closeSender();
    if (receiveLine(sockets.receiver())) {
        std::cerr << "FAIL: incomplete line was accepted\n";
        return false;
    }

    return true;
}

bool testReceiveTimeout() {
    SocketPair sockets;
    if (!sockets.open()) {
        return false;
    }

    timeval timeout{};
    timeout.tv_usec = 100000;  // 测试用 100 ms，服务端仍使用 5 秒。
    if (setsockopt(sockets.receiver(), SOL_SOCKET, SO_RCVTIMEO,
                   &timeout, sizeof(timeout)) == -1) {
        std::perror("setsockopt failed");
        return false;
    }

    // 发送端保持打开但不发送，确保测试的是等待超时而不是 EOF。
    const auto start = std::chrono::steady_clock::now();
    const auto line = receiveLine(sockets.receiver());
    const auto elapsed = std::chrono::steady_clock::now() - start;

    if (line) {
        std::cerr << "FAIL: received a line without any peer data\n";
        return false;
    }

    // 宽松的下限能发现立即失败，避免要求耗时精确等于 100 ms。
    if (elapsed < std::chrono::milliseconds(50)) {
        std::cerr << "FAIL: receive returned before the timeout wait\n";
        return false;
    }

    return true;
}

}  // namespace

int main() {
    const struct TestCase {
        const char* name;
        bool (*run)();
    } tests[] = {
        {"complete line", testCompleteLine},
        {"empty line", testEmptyLine},
        {"128-byte line", testMaximumLengthLine},
        {"overlong line", testRejectsOverlongLine},
        {"incomplete line", testRejectsIncompleteLine},
        {"receive timeout", testReceiveTimeout},
    };

    for (const auto& test : tests) {
        if (!test.run()) {
            std::cerr << "FAIL: " << test.name << '\n';
            return 1;
        }
        std::cout << "PASS: " << test.name << '\n';
    }

    return 0;
}
