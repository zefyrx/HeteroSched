#include <arpa/inet.h>
#include <sys/socket.h>
#include <poll.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "net/epoll_server.h"
#include "net/unique_fd.h"

namespace {
using Clock = std::chrono::steady_clock;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

UniqueFd connectTo(EpollServer& server, bool pollListener = true) {
    UniqueFd client(socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0));
    require(client.get() >= 0, "socket failed");
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(server.port());
    require(connect(client.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0,
            "connect failed");
    // 先接受连接，使调用方能确定连接已进入事件循环。
    if (pollListener) server.pollOnce(0);
    return client;
}

void sendRequest(int fd, const std::string& request) {
    require(send(fd, request.data(), request.size(), MSG_NOSIGNAL) ==
            static_cast<ssize_t>(request.size()), "test request send failed");
}

void requireIdleOpen(int fd) {
    char byte;
    require(recv(fd, &byte, 1, MSG_DONTWAIT | MSG_PEEK) < 0 &&
            (errno == EAGAIN || errno == EWOULDBLOCK), "idle connection closed too early");
}

void requireEof(int fd) {
    pollfd event{fd, POLLIN, 0};
    require(poll(&event, 1, 2000) == 1, "expired connection did not become readable");
    char byte;
    require(recv(fd, &byte, 1, MSG_DONTWAIT) == 0, "expired connection did not close");
}

void requireRejected(int fd) {
    pollfd event{fd, POLLIN, 0};
    require(poll(&event, 1, 2000) == 1, "over-limit connection was not closed");
    char byte;
    const auto count = recv(fd, &byte, 1, MSG_DONTWAIT);
    require(count == 0 || (count < 0 && errno == ECONNRESET),
            "over-limit connection received data or remained open");
}

std::string readLine(int fd) {
    std::string response;
    while (response.find('\n') == std::string::npos) {
        pollfd event{fd, POLLIN, 0};
        require(poll(&event, 1, 2000) == 1, "response timed out");
        char buffer[64];
        const auto count = recv(fd, buffer, sizeof(buffer), MSG_DONTWAIT);
        require(count > 0, "response closed or unreadable");
        response.append(buffer, static_cast<std::size_t>(count));
        require(response.size() <= 64, "unexpected response size");
    }
    return response;
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

void testBacklogBeforeEventLoopStarts() {
    EpollServer server(0, [](const std::string&) { return std::string("OK\n"); }, 128);
    std::vector<UniqueFd> clients;
    // 不运行事件循环，所有连接都在 listen 队列中；默认 backlog=1 无法容纳该突发。
    for (int index = 0; index < 8; ++index) {
        UniqueFd client(socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0));
        require(client.get() >= 0, "socket failed");
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(server.port());
        const int connected = connect(client.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address));
        require(connected == 0 || errno == EINPROGRESS, "nonblocking connect failed");
        pollfd event{client.get(), POLLOUT, 0};
        require(poll(&event, 1, 2000) == 1, "configured backlog did not hold queued connections");
        int error = 0;
        socklen_t length = sizeof(error);
        require(getsockopt(client.get(), SOL_SOCKET, SO_ERROR, &error, &length) == 0 && error == 0,
                "queued connection failed");
        clients.push_back(std::move(client));
    }
    for (int backlog : {0, -1}) {
        bool rejected = false;
        try {
            EpollServer invalid(0, [](const std::string&) { return std::string("OK\n"); }, backlog);
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        require(rejected, "nonpositive backlog must be rejected by server API");
    }
}

void testIdleDeadlineAndIncompleteRequest() {
    auto now = Clock::time_point{};
    int handled = 0;
    EpollServer server(0, [&](const std::string&) { ++handled; return std::string("OK\n"); },
                       1, std::chrono::milliseconds{100}, 0, [&] { return now; });
    auto silent = connectTo(server);
    now += std::chrono::milliseconds{99};
    server.pollOnce(0);
    requireIdleOpen(silent.get());
    now += std::chrono::milliseconds{1};
    server.pollOnce(0);
    requireEof(silent.get());

    auto partial = connectTo(server);
    sendRequest(partial.get(), "PING");
    server.pollOnce(0);
    now += std::chrono::milliseconds{100};
    server.pollOnce(0);
    requireEof(partial.get());
    require(handled == 0, "expired incomplete request reached handler");
    auto healthy = connectTo(server);
    sendRequest(healthy.get(), "PING\n");
    server.pollOnce(0);
    require(readLine(healthy.get()) == "OK\n", "idle expiry stopped new clients");
}

void testActivityRenewsIdleDeadline() {
    auto now = Clock::time_point{};
    EpollServer server(0, [](const std::string&) { return std::string("OK\n"); },
                       1, std::chrono::milliseconds{100}, 0, [&] { return now; });
    auto client = connectTo(server);
    now += std::chrono::milliseconds{60};
    sendRequest(client.get(), "PI");
    server.pollOnce(0);
    now += std::chrono::milliseconds{60};  // 已过 accept+100，但最近一次读取仅过去 60 ms。
    server.pollOnce(0);
    requireIdleOpen(client.get());
    sendRequest(client.get(), "NG\n");
    server.pollOnce(0);
    require(readLine(client.get()) == "OK\n", "fragment activity did not renew deadline");
    now += std::chrono::milliseconds{99};
    server.pollOnce(0);
    requireIdleOpen(client.get());
    now += std::chrono::milliseconds{1};
    server.pollOnce(0);
    requireEof(client.get());
}

void testIndependentIdleDeadlines() {
    auto now = Clock::time_point{};
    EpollServer server(0, [](const std::string&) { return std::string("OK\n"); },
                       1, std::chrono::milliseconds{100}, 0, [&] { return now; });
    auto first = connectTo(server);
    now += std::chrono::milliseconds{50};
    auto second = connectTo(server);
    now += std::chrono::milliseconds{50};
    server.pollOnce(0);
    requireEof(first.get());
    requireIdleOpen(second.get());
    sendRequest(second.get(), "PING\n");
    server.pollOnce(0);
    require(readLine(second.get()) == "OK\n", "one expiry affected another connection");
    auto third = connectTo(server);
    now += std::chrono::milliseconds{100};
    server.pollOnce(0);
    requireEof(second.get());
    requireEof(third.get());
}

void testPendingWritesRenewAndEventuallyExpire() {
    const std::string large(8 * 1024 * 1024, 'z');
    auto now = Clock::time_point{};
    EpollServer server(0, [&](const std::string&) { return large; },
                       1, std::chrono::milliseconds{100}, 0, [&] { return now; });
    auto active = connectTo(server);
    sendRequest(active.get(), "BIG\n");
    require(shutdown(active.get(), SHUT_WR) == 0, "shutdown failed");
    server.pollOnce(0);  // 只发送 64 KiB，后续必须等待 EPOLLOUT。
    now += std::chrono::milliseconds{80};
    server.pollOnce(0);  // 成功发送继续刷新截止时间。
    now += std::chrono::milliseconds{40};
    require(drain(server, active.get()) == large, "positive writes did not renew idle deadline");

    auto stalled = connectTo(server);
    // 保持默认 TCP 窗口；8 MiB 输出足以形成积压，不额外放慢关闭后的数据排空。
    sendRequest(stalled.get(), "BIG\n");
    require(shutdown(stalled.get(), SHUT_WR) == 0, "shutdown failed");
    for (int iteration = 0; iteration < 128; ++iteration) server.pollOnce(0);
    now += std::chrono::milliseconds{100};
    server.pollOnce(0);
    const auto partialResponse = drain(server, stalled.get());
    require(!partialResponse.empty() && partialResponse.size() < large.size(),
            "stalled output must be discarded on idle expiry");
}

void testIdleTimeoutDisabledAndInvalidOptions() {
    auto now = Clock::time_point{};
    EpollServer server(0, [](const std::string&) { return std::string("OK\n"); },
                       1, std::chrono::milliseconds{0}, 0, [&] { return now; });
    auto client = connectTo(server);
    now += std::chrono::hours{24};
    server.pollOnce(0);
    requireIdleOpen(client.get());
    sendRequest(client.get(), "PING\n");
    server.pollOnce(0);
    require(readLine(client.get()) == "OK\n", "disabled timeout expired connection");
    for (const auto milliseconds : {-1LL, 2147483648LL}) {
        bool rejected = false;
        try {
            EpollServer invalid(0, [](const std::string&) { return std::string(); },
                                1, std::chrono::milliseconds{milliseconds});
        } catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "invalid idle timeout was accepted");
    }
    bool rejected = false;
    try {
        EpollServer invalid(0, [](const std::string&) { return std::string(); },
                            1, std::chrono::milliseconds{100}, 0, EpollServer::Now{});
    } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "empty clock was accepted");
}

void testConnectionLimitAndRejectedBurst() {
    int handled = 0;
    EpollServer server(0, [&](const std::string&) { ++handled; return std::string("OK\n"); },
                       128, std::chrono::milliseconds{0}, 2);
    auto first = connectTo(server);
    auto second = connectTo(server);
    for (int fd : {first.get(), second.get()}) {
        sendRequest(fd, "PING\n");
        server.pollOnce(0);
        require(readLine(fd) == "OK\n", "connection at capacity was rejected");
    }
    std::vector<UniqueFd> excess;
    // 96 个连接先在 backlog=128 排队，超过单轮 accept 预算 64。
    for (int index = 0; index < 96; ++index) {
        auto client = connectTo(server, false);
        sendRequest(client.get(), "PING\n");
        excess.push_back(std::move(client));
    }
    server.pollOnce(0);
    sendRequest(first.get(), "PING\n");
    server.pollOnce(0);
    require(readLine(first.get()) == "OK\n", "rejected burst blocked an existing client");
    for (const auto& client : excess) requireRejected(client.get());
    require(handled == 3, "rejected connection reached request handler");

    require(shutdown(first.get(), SHUT_WR) == 0, "shutdown failed");
    server.pollOnce(0);
    requireEof(first.get());
    auto replacement = connectTo(server);
    sendRequest(replacement.get(), "PING\n");
    server.pollOnce(0);
    require(readLine(replacement.get()) == "OK\n", "disconnect did not release a slot");
    sendRequest(second.get(), "PING\n");
    server.pollOnce(0);
    require(readLine(second.get()) == "OK\n", "slot recovery affected another connection");
}

void testPendingOutputHoldsConnectionSlot() {
    const std::string large(8 * 1024 * 1024, 'q');
    EpollServer server(0, [&](const std::string& line) {
        return line == "BIG" ? large : std::string("OK\n");
    }, 128, std::chrono::milliseconds{0}, 1);
    auto slow = connectTo(server);
    sendRequest(slow.get(), "BIG\n");
    require(shutdown(slow.get(), SHUT_WR) == 0, "shutdown failed");
    server.pollOnce(0);
    auto rejected = connectTo(server);
    requireRejected(rejected.get());
    require(drain(server, slow.get()) == large, "capacity handling lost pending output");
    auto replacement = connectTo(server);
    sendRequest(replacement.get(), "PING\n");
    require(shutdown(replacement.get(), SHUT_WR) == 0, "shutdown failed");
    require(drain(server, replacement.get()) == "OK\n", "flushed output did not release slot");
}

void testResetAndIdleExpiryReleaseSlots() {
    auto now = Clock::time_point{};
    EpollServer server(0, [](const std::string&) { return std::string("OK\n"); },
                       128, std::chrono::milliseconds{100}, 1, [&] { return now; });
    auto resetClient = connectTo(server);
    linger reset{1, 0};
    require(setsockopt(resetClient.get(), SOL_SOCKET, SO_LINGER, &reset, sizeof(reset)) == 0,
            "setsockopt linger failed");
    resetClient = UniqueFd();
    server.pollOnce(0);
    auto idle = connectTo(server);
    sendRequest(idle.get(), "PING\n");
    server.pollOnce(0);
    require(readLine(idle.get()) == "OK\n", "reset did not release a slot");
    auto excess = connectTo(server);
    requireRejected(excess.get());
    now += std::chrono::milliseconds{100};
    server.pollOnce(0);
    requireEof(idle.get());
    auto replacement = connectTo(server);
    sendRequest(replacement.get(), "PING\n");
    server.pollOnce(0);
    require(readLine(replacement.get()) == "OK\n", "idle expiry did not release a slot");
    bool rejected = false;
    try {
        EpollServer invalid(0, [](const std::string&) { return std::string(); },
                            1, std::chrono::milliseconds{0}, -1);
    } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "negative connection limit was accepted");
}
}  // namespace

int main() {
    try {
        testPendingOutputAndResume();
        std::cout << "PASS: pending output resumes; slow reader isolation; response order; half-close\n";
        testDisconnectWithPendingOutput();
        std::cout << "PASS: disconnect while output is pending\n";
        testBacklogBeforeEventLoopStarts();
        std::cout << "PASS: configured backlog accepts queued burst before event loop; invalid backlog rejected\n";
        testIdleDeadlineAndIncompleteRequest();
        std::cout << "PASS: exact idle deadline, incomplete request discarded, new clients continue\n";
        testActivityRenewsIdleDeadline();
        std::cout << "PASS: byte activity renews idle deadline\n";
        testIndependentIdleDeadlines();
        std::cout << "PASS: independent deadlines and simultaneous expiry\n";
        testPendingWritesRenewAndEventuallyExpire();
        std::cout << "PASS: positive writes renew deadline, stalled output expires\n";
        testIdleTimeoutDisabledAndInvalidOptions();
        std::cout << "PASS: timeout disabled by default, invalid settings rejected\n";
        testConnectionLimitAndRejectedBurst();
        std::cout << "PASS: capacity boundary, 96 rejected clients, existing service and disconnect recovery\n";
        testPendingOutputHoldsConnectionSlot();
        std::cout << "PASS: half-closed connection holds slot until pending output is flushed\n";
        testResetAndIdleExpiryReleaseSlots();
        std::cout << "PASS: reset and idle expiry release slots; negative limit rejected\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
