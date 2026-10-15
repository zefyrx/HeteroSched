#include "job_completion.h"

#include <sstream>

std::optional<int> parseJobCompletion(const std::string& line) {
    std::istringstream input(line);
    std::string command;
    int jobId = 0;
    std::string extra;

    if (!(input >> command >> jobId) || command != "COMPLETE_JOB" ||
        jobId < 0 || (input >> extra)) {
        return std::nullopt;
    }

    return jobId;
}
