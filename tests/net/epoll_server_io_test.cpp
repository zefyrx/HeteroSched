#include <arpa/inet.h>
#include <sys/socket.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>

#include "net/epoll_server.h"
#include "net/unique_fd.h"

namespace {
using Clock = std::chrono::steady_clock;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

UniqueFd connectTo(EpollServer& server) {
    UniqueFd client(socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0));
    require(client.get() >= 0, "socket failed");
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(server.port());
    require(connect(client.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0,
            "connect failed");
    // 先接受连接，避免测试被 backlog=1 限制影响。
    server.pollOnce(0);
    return client;
}

void sendRequest(int fd, const std::string& request) {
    require(send(fd, request.data(), request.size(), MSG_NOSIGNAL) ==
            static_cast<ssize_t>(request.size()), "test request send failed");
}

std::string drain(EpollServer& server, int fd) {
    std::string response;
    std::array<char, 64 * 1024> buffer{};
    const auto deadline = Clock::now() + std::chrono::seconds(10);
    while (Clock::now() < deadline) {
        server.pollOnce(1);
        const ssize_t count = recv(fd, buffer.data(), buffer.size(), MSG_DONTWAIT);
        if (count > 0) response.append(buffer.data(), static_cast<std::size_t>(count));
        else if (count == 0) return response;
        else require(errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR,
                     "unexpected recv error");
    }
    throw std::runtime_error("response did not finish before deadline");
}

void testPendingOutputAndResume() {
    // 实际协议响应很短；注入大响应来覆盖多次发送、积压和恢复路径。
    const std::string large(8 * 1024 * 1024, 'x');
    EpollServer server(0, [&](const std::string& line) {
        return line == "BIG" ? large : std::string("OK\n");
    });
    auto slow = connectTo(server);
    sendRequest(slow.get(), "BIG\nPING\n");
    require(shutdown(slow.get(), SHUT_WR) == 0, "shutdown failed");
    // 暂时不读响应。单轮最多发送 64 KiB，无法在这些轮次中发完 8 MiB。
    for (int iteration = 0; iteration < 16; ++iteration) server.pollOnce(0);
    auto fast = connectTo(server);
    sendRequest(fast.get(), "PING\n");
    require(shutdown(fast.get(), SHUT_WR) == 0, "shutdown failed");
    require(drain(server, fast.get()) == "OK\n", "slow reader blocked another client");
    require(drain(server, slow.get()) == large + "OK\n",
            "pending output lost, duplicated, reordered, or half-close lost response");
}

void testDisconnectWithPendingOutput() {
    const std::string large(8 * 1024 * 1024, 'y');
    EpollServer server(0, [&](const std::string& line) {
        return line == "BIG" ? large : std::string("OK\n");
    });
    auto disconnected = connectTo(server);
    sendRequest(disconnected.get(), "BIG\n");
    for (int iteration = 0; iteration < 8; ++iteration) server.pollOnce(0);
    linger reset{1, 0};
    require(setsockopt(disconnected.get(), SOL_SOCKET, SO_LINGER, &reset, sizeof(reset)) == 0,
            "setsockopt linger failed");
    disconnected = UniqueFd();
    auto healthy = connectTo(server);
    sendRequest(healthy.get(), "PING\n");
    require(shutdown(healthy.get(), SHUT_WR) == 0, "shutdown failed");
    require(drain(server, healthy.get()) == "OK\n", "disconnect stopped the event loop");
}
}  // namespace

int main() {
    try {
        testPendingOutputAndResume();
        std::cout << "PASS: pending output resumes; slow reader isolation; response order; half-close\n";
        testDisconnectWithPendingOutput();
        std::cout << "PASS: disconnect while output is pending\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
