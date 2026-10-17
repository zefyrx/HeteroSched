#ifndef HETEROSCHED_EPOLL_SERVER_H
#define HETEROSCHED_EPOLL_SERVER_H

#include <cstddef>
#include <cstdint>
#include <chrono>
#include <functional>
#include <string>
#include <unordered_map>

#include "net/unique_fd.h"

// 单线程 LT 事件循环，只管理连接和字节流；业务通过 handler 注入。
class EpollServer {
public:
    using RequestHandler = std::function<std::string(const std::string&)>;
    using Clock = std::chrono::steady_clock;
    using Now = std::function<Clock::time_point()>;

    // port=0 由系统选择空闲端口；backlog 默认 1，以复现原基线。
    // idleTimeout=0 关闭空闲清理；now 必须单调，测试可注入可控时钟。
    // maxConnections=0 不限制；否则只保留至多指定数量的客户端状态。
    EpollServer(std::uint16_t port, RequestHandler handler, int backlog = 1,
                std::chrono::milliseconds idleTimeout = std::chrono::milliseconds{0},
                int maxConnections = 0, Now now = Clock::now);
    std::uint16_t port() const noexcept { return port_; }
    void run();
    void pollOnce(int timeoutMs = -1);

private:
    struct Connection {
        Connection(UniqueFd socket, Clock::time_point acceptedAt)
            : fd(std::move(socket)), lastActivity(acceptedAt) {}
        UniqueFd fd;
        Clock::time_point lastActivity;
        std::string input;
        std::string output;
        std::size_t outputOffset = 0;
        bool readClosed = false;       // recv() 已返回 EOF。
        bool closing = false;          // 非法帧：发完之前的响应后关闭。
        std::uint32_t interests = 0;
        std::size_t pendingBytes() const { return output.size() - outputOffset; }
    };

    UniqueFd listener_;
    UniqueFd epoll_;
    RequestHandler handler_;
    std::chrono::milliseconds idleTimeout_;
    int maxConnections_;
    Now now_;
    std::uint16_t port_ = 0;
    // 使用连接代号而非裸 fd，避免同批事件里的旧事件命中复用后的 fd。
    std::uint64_t nextToken_ = 1;
    std::unordered_map<std::uint64_t, Connection> connections_;

    void acceptReady();
    void processInput(Connection& connection);
    bool readReady(Connection& connection);
    bool writeReady(Connection& connection);
    bool service(Connection& connection, std::uint32_t events);
    bool updateInterests(std::uint64_t token, Connection& connection);
    void closeConnection(std::uint64_t token);
    void recordActivity(Connection& connection);
    void expireIdleConnections(Clock::time_point now);
    int waitTimeout(int requestedMs, Clock::time_point now) const;
};

#endif
