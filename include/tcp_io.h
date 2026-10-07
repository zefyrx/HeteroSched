#ifndef HETEROSCHED_TCP_IO_H
#define HETEROSCHED_TCP_IO_H

#include <string>
#include <optional>

bool sendAll(int fd, const std::string& message);

std::optional<std::string> receiveLine(int fd);

#endif
