#ifndef HETEROSCHED_UNIQUE_FD_H
#define HETEROSCHED_UNIQUE_FD_H

#include <unistd.h>
#include <utility>

// 一个对象独占一个 fd；离开作用域时关闭，避免错误分支遗漏清理。
class UniqueFd {
public:
    explicit UniqueFd(int fd = -1) noexcept : fd_(fd) {}
    ~UniqueFd() { if (fd_ >= 0) close(fd_); }
    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;
    UniqueFd(UniqueFd&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}
    UniqueFd& operator=(UniqueFd&& other) noexcept {
        if (this != &other) {
            if (fd_ >= 0) close(fd_);
            fd_ = std::exchange(other.fd_, -1);
        }
        return *this;
    }
    int get() const noexcept { return fd_; }

private:
    int fd_;
};

#endif
