#include "protocol/registration.h"

#include <sstream>

std::optional<Worker> parseRegistration(const std::string& line) {
    std::istringstream input(line);

    std::string command;
    int workerId = 0;
    int cpuCores = 0;
    int memoryMB = 0;

    // 提取 command 和三个整数。
    // 提取失败则返回 std::nullopt。
    if (! (input >> command >> workerId >> cpuCores >> memoryMB)) {
        return std::nullopt;
    }

    // command 必须等于 "REGISTER_WORKER"。
    if (command != "REGISTER_WORKER") {
        return std::nullopt;
    }

    // 拒绝任何额外的非空白字段。
    std::string extra;
    if (input >> extra) {
        return std::nullopt;
    }

    const Resource resource{cpuCores, memoryMB};

    // workerId >= 0，且 hasPositiveResources(resource)。
    if (!(workerId >= 0 && hasPositiveResources(resource))) {
        return std::nullopt;
    }

    return Worker{workerId, resource};
}