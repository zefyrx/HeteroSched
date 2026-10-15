#include <iostream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "protocol/job_completion.h"

int main() {
    const int maximum = std::numeric_limits<int>::max();
    const std::vector<std::pair<std::string, int>> validMessages = {
        {"COMPLETE_JOB 101", 101},
        {"COMPLETE_JOB 0", 0},
        {" \tCOMPLETE_JOB\t42  ", 42},
        {"COMPLETE_JOB +1", 1},
        {"COMPLETE_JOB " + std::to_string(maximum), maximum},
    };
    for (const auto& entry : validMessages) {
        const auto id = parseJobCompletion(entry.first);
        if (!id || *id != entry.second) {
            std::cerr << "FAIL: valid completion rejected: " << entry.first << '\n';
            return 1;
        }
    }

    const std::vector<std::string> invalidMessages = {
        "", " \t", "COMPLETE_JOB", "COMPLETE_JOB -1",
        "COMPLETE_JOB abc", "COMPLETE_JOB 1abc", "COMPLETE_JOB 1.5",
        "COMPLETE_JOB 1 extra", "COMPLETE_JOB 1 2", "COMPLETE_JOB 1+2",
        "COMPLETE_JOB 999999999999999999999",
        "COMPLETE_JOB -999999999999999999999",
        "COMPLETE_JOB_EXTRA 1", "complete_job 1", "SUBMIT_JOB 1",
    };
    for (const auto& message : invalidMessages) {
        if (parseJobCompletion(message)) {
            std::cerr << "FAIL: invalid completion accepted: " << message << '\n';
            return 1;
        }
    }

    std::cout << "PASS: completion message parsing and integer boundaries\n";
    return 0;
}
