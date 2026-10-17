#ifndef HETEROSCHED_EPOLL_SERVER_H
#define HETEROSCHED_EPOLL_SERVER_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>

#include "net/unique_fd.h"

// 单线程 LT 事件循环，只管理连接和字节流；业务通过 handler 注入。
class EpollServer {
public:
    using RequestHandler = std::function<std::string(const std::string&)>;

    // port=0 由系统选择空闲端口；backlog 默认 1，以复现原基线。
    EpollServer(std::uint16_t port, RequestHandler handler, int backlog = 1);
    std::uint16_t port() const noexcept { return port_; }
    void run();
    void pollOnce(int timeoutMs = -1);

private:
    struct Connection {
        explicit Connection(UniqueFd socket) : fd(std::move(socket)) {}
        UniqueFd fd;
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
};

#endif
