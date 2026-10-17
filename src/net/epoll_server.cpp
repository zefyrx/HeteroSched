#include "net/epoll_server.h"

#include <arpa/inet.h>
#include <sys/epoll.h>
#include <sys/socket.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <limits>
#include <stdexcept>
#include <system_error>

#include "net/tcp_io.h"

namespace {
constexpr std::size_t kOutputHighWater = 64 * 1024;
constexpr std::size_t kIoBudget = 64 * 1024;
constexpr int kAcceptBudget = 64;

[[noreturn]] void fail(const char* operation) {
    throw std::system_error(errno, std::generic_category(), operation);
}

bool wouldBlock() { return errno == EAGAIN || errno == EWOULDBLOCK; }
}  // namespace

EpollServer::EpollServer(std::uint16_t port, RequestHandler handler, int backlog,
                         std::chrono::milliseconds idleTimeout, int maxConnections, Now now)
    : handler_(std::move(handler)), idleTimeout_(idleTimeout),
      maxConnections_(maxConnections), now_(std::move(now)) {
    if (backlog <= 0) throw std::invalid_argument("backlog must be positive");
    if (maxConnections_ < 0) throw std::invalid_argument("max connections must be nonnegative");
    if (idleTimeout_.count() < 0 || idleTimeout_.count() > std::numeric_limits<int>::max())
        throw std::invalid_argument("idle timeout must be a nonnegative int in milliseconds");
    if (!now_) throw std::invalid_argument("monotonic clock is required");
    listener_ = UniqueFd(socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0));
    if (listener_.get() < 0) fail("socket");
    epoll_ = UniqueFd(epoll_create1(EPOLL_CLOEXEC));
    if (epoll_.get() < 0) fail("epoll_create1");
    if (!handler_) throw std::invalid_argument("request handler is required");

    const int reuseAddress = 1;
    if (setsockopt(listener_.get(), SOL_SOCKET, SO_REUSEADDR, &reuseAddress, sizeof(reuseAddress)) < 0)
        fail("setsockopt SO_REUSEADDR");

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    if (bind(listener_.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) < 0)
        fail("bind");
    if (listen(listener_.get(), backlog) < 0) fail("listen");
    socklen_t length = sizeof(address);
    if (getsockname(listener_.get(), reinterpret_cast<sockaddr*>(&address), &length) < 0)
        fail("getsockname");
    port_ = ntohs(address.sin_port);

    epoll_event event{};
    event.events = EPOLLIN;  // 不设置 EPOLLET，即 LT。
    event.data.u64 = 0;     // 0 专门代表监听 socket。
    if (epoll_ctl(epoll_.get(), EPOLL_CTL_ADD, listener_.get(), &event) < 0)
        fail("epoll_ctl ADD listener");
}

void EpollServer::acceptReady() {
    for (int accepted = 0; accepted < kAcceptBudget; ++accepted) {
        UniqueFd client(accept4(listener_.get(), nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC));
        if (client.get() < 0) {
            if (errno == EINTR || errno == ECONNABORTED || errno == EPROTO) continue;
            if (wouldBlock()) return;
            fail("accept4");
        }
        if (maxConnections_ > 0 && connections_.size() >= static_cast<std::size_t>(maxConnections_))
            continue;  // UniqueFd 立即关闭；不创建缓冲区、不调用 handler，仍计入 accept 预算。
        const std::uint64_t token = nextToken_++;
        const auto acceptedAt = idleTimeout_.count() > 0 ? now_() : Clock::time_point{};
        auto [iterator, inserted] = connections_.try_emplace(token, std::move(client), acceptedAt);
        (void)inserted;
        auto& connection = iterator->second;
        epoll_event event{};
        event.events = EPOLLIN | EPOLLRDHUP;
        event.data.u64 = token;
        if (epoll_ctl(epoll_.get(), EPOLL_CTL_ADD, connection.fd.get(), &event) < 0)
            fail("epoll_ctl ADD client");
        connection.interests = event.events;
    }
    // 达到公平性预算时让出本轮；LT 会再次通知尚未 accept 的连接。
}

void EpollServer::processInput(Connection& connection) {
    if (connection.closing) return;
    if (connection.outputOffset >= kOutputHighWater || connection.pendingBytes() == 0) {
        connection.output.erase(0, connection.outputOffset);
        connection.outputOffset = 0;
    }
    while (connection.pendingBytes() < kOutputHighWater) {
        const auto newline = connection.input.find('\n');
        if (newline == std::string::npos) {
            if (connection.input.size() > kMaxMessageLength) connection.closing = true;
            if (connection.closing || connection.readClosed) connection.input.clear();
            return;
        }
        if (newline > kMaxMessageLength) {
            connection.closing = true;
            connection.input.clear();
            return;
        }
        const std::string line = connection.input.substr(0, newline);
        connection.input.erase(0, newline + 1);
        connection.output += handler_(line);
    }
}

bool EpollServer::readReady(Connection& connection) {
    std::array<char, 4096> buffer{};
    std::size_t received = 0;
    while (!connection.readClosed && !connection.closing &&
           connection.pendingBytes() < kOutputHighWater && received < kIoBudget) {
        const auto length = std::min(buffer.size(), kIoBudget - received);
        const ssize_t count = recv(connection.fd.get(), buffer.data(), length, 0);
        if (count > 0) {
            recordActivity(connection);
            received += static_cast<std::size_t>(count);
            connection.input.append(buffer.data(), static_cast<std::size_t>(count));
            processInput(connection);
        } else if (count == 0) {
            connection.readClosed = true;
            processInput(connection);
        } else {
            if (errno == EINTR) continue;
            if (wouldBlock()) return true;
            return false;
        }
    }
    return true;
}

bool EpollServer::writeReady(Connection& connection) {
    std::size_t sent = 0;
    while (connection.pendingBytes() > 0 && sent < kIoBudget) {
        const auto length = std::min(connection.pendingBytes(), kIoBudget - sent);
        const ssize_t count = send(connection.fd.get(),
                                   connection.output.data() + connection.outputOffset,
                                   length, MSG_NOSIGNAL);
        if (count > 0) {
            recordActivity(connection);
            connection.outputOffset += static_cast<std::size_t>(count);
            sent += static_cast<std::size_t>(count);
        } else if (count < 0 && errno == EINTR) {
            continue;
        } else if (count < 0 && wouldBlock()) {
            return true;  // offset 保留，等 EPOLLOUT 再继续。
        } else {
            return false;
        }
    }
    if (connection.pendingBytes() == 0) {
        connection.output.clear();
        connection.outputOffset = 0;
    }
    return true;
}

bool EpollServer::service(Connection& connection, std::uint32_t events) {
    if (events & EPOLLERR) return false;
    processInput(connection);
    // RDHUP 只表示对端不再发送，仍需读完剩余字节并发送响应。
    if ((events & (EPOLLIN | EPOLLRDHUP | EPOLLHUP)) && !readReady(connection)) return false;
    if (!writeReady(connection)) return false;
    // 发送腾出空间后，继续处理已在用户态缓冲区里的完整行。
    processInput(connection);
    if (events & EPOLLHUP) return false;
    return !((connection.readClosed || connection.closing) && connection.pendingBytes() == 0);
}

bool EpollServer::updateInterests(std::uint64_t token, Connection& connection) {
    std::uint32_t interests = 0;
    if (!connection.readClosed && !connection.closing && connection.pendingBytes() < kOutputHighWater)
        interests |= EPOLLIN | EPOLLRDHUP;
    if (connection.pendingBytes() > 0) interests |= EPOLLOUT;
    if (interests == connection.interests) return true;
    epoll_event event{};
    event.events = interests;
    event.data.u64 = token;
    if (epoll_ctl(epoll_.get(), EPOLL_CTL_MOD, connection.fd.get(), &event) < 0) return false;
    connection.interests = interests;
    return true;
}

void EpollServer::closeConnection(std::uint64_t token) {
    const auto iterator = connections_.find(token);
    if (iterator == connections_.end()) return;
    epoll_ctl(epoll_.get(), EPOLL_CTL_DEL, iterator->second.fd.get(), nullptr);
    connections_.erase(iterator);  // UniqueFd 自动关闭 socket。
}

void EpollServer::recordActivity(Connection& connection) {
    if (idleTimeout_.count() > 0) connection.lastActivity = now_();
}

void EpollServer::expireIdleConnections(Clock::time_point now) {
    if (idleTimeout_.count() == 0) return;
    for (auto iterator = connections_.begin(); iterator != connections_.end();) {
        const auto token = iterator->first;
        const bool expired = now - iterator->second.lastActivity >= idleTimeout_;
        ++iterator;  // closeConnection 会 erase，先保存下一个迭代器。
        if (expired) closeConnection(token);
    }
}

int EpollServer::waitTimeout(int requestedMs, Clock::time_point now) const {
    if (idleTimeout_.count() == 0) return requestedMs;
    int result = requestedMs;
    for (const auto& entry : connections_) {
        const auto remaining = entry.second.lastActivity + idleTimeout_ - now;
        // 向上取整，避免不足 1 ms 时反复以 0 ms 等待形成忙循环。
        const auto milliseconds = std::chrono::ceil<std::chrono::milliseconds>(remaining).count();
        const int limit = static_cast<int>(std::clamp<std::int64_t>(
            milliseconds, 0, std::numeric_limits<int>::max()));
        if (result < 0 || limit < result) result = limit;
    }
    return result;
}

void EpollServer::pollOnce(int timeoutMs) {
    const auto beforeWait = idleTimeout_.count() > 0 ? now_() : Clock::time_point{};
    expireIdleConnections(beforeWait);
    std::array<epoll_event, 64> events{};
    const int count = epoll_wait(epoll_.get(), events.data(), events.size(),
                                waitTimeout(timeoutMs, beforeWait));
    if (count < 0) {
        if (errno == EINTR) return;
        fail("epoll_wait");
    }
    // 截止时间已到的连接先清理；同批旧事件由连接代号查找自然跳过。
    if (idleTimeout_.count() > 0) expireIdleConnections(now_());
    for (int index = 0; index < count; ++index) {
        const auto token = events[index].data.u64;
        if (token == 0) {
            if (events[index].events & (EPOLLERR | EPOLLHUP))
                throw std::runtime_error("listening socket disconnected");
            acceptReady();
            continue;
        }
        const auto iterator = connections_.find(token);
        if (iterator == connections_.end()) continue;
        if (!service(iterator->second, events[index].events) ||
            !updateInterests(token, iterator->second)) closeConnection(token);
    }
}

void EpollServer::run() {
    while (true) pollOnce();
}
