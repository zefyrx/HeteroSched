#include "protocol/job_submission.h"

#include <sstream>

std::optional<Job> parseJobSubmission(const std::string& line) {
    std::istringstream input(line);

    std::string command;
    int jobId = 0;
    int cpuCores = 0;
    int memoryMB = 0;

    // 提取 command 和三个整数。
    // 提取失败则返回 std::nullopt。
    if (! (input >> command >> jobId >> cpuCores >> memoryMB)) {
        return std::nullopt;
    }

    // command 必须等于 "SUBMIT_JOB"。
    if (command != "SUBMIT_JOB") {
        return std::nullopt;
    }

    // 拒绝任何额外的非空白字段。
    std::string extra;
    if (input >> extra) {
        return std::nullopt;
    }

    const Resource resource{cpuCores, memoryMB};

    // jobId >= 0，且 hasPositiveResources(resource)。
    if (!(jobId >= 0 && hasPositiveResources(resource))) {
        return std::nullopt;
    }

    return Job{jobId, Resource{cpuCores, memoryMB}};
}
