#ifndef HETEROSCHED_TCP_IO_H
#define HETEROSCHED_TCP_IO_H

#include <string>
#include <optional>
#include <cstddef>

inline constexpr std::size_t kMaxMessageLength = 128;

bool sendAll(int fd, const std::string& message);

std::optional<std::string> receiveLine(int fd);

#endif
