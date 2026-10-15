#include <iostream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "scheduler_protocol.h"

namespace {

bool expectResponse(Scheduler& scheduler, const std::string& request,
                    const std::string& expected) {
    const auto response = processSchedulerRequest(request, scheduler);
    if (response != expected) {
        std::cerr << "FAIL: request [" << request << "] returned ["
                  << response << "] instead of [" << expected << "]\n";
        return false;
    }
    return true;
}

bool expectState(const Scheduler& scheduler, std::size_t waiting,
                 std::size_t running, int availableCpu, int availableMemory) {
    const auto workers = scheduler.getWorkerSnapshots();
    if (scheduler.waitingJobCount() != waiting ||
        scheduler.runningJobCount() != running || workers.size() != 1 ||
        workers[0].workerId != 1 ||
        workers[0].totalResource.cpuCores != 2 ||
        workers[0].totalResource.memoryMB != 4096 ||
        workers[0].availableResource.cpuCores != availableCpu ||
        workers[0].availableResource.memoryMB != availableMemory) {
        std::cerr << "FAIL: queue, running records or resources changed incorrectly\n";
        return false;
    }
    return true;
}

bool testSchedulingAndCompletionLifecycle() {
    Scheduler scheduler;
    if (!expectResponse(scheduler, "SCHEDULE_NEXT", "SCHEDULE_NONE\n") ||
        !expectResponse(scheduler, "SUBMIT_JOB 101 2 4096", "SUBMIT_OK\n") ||
        !expectResponse(scheduler, "SUBMIT_JOB 102 1 1024", "SUBMIT_OK\n") ||
        !expectResponse(scheduler, "SCHEDULE_NEXT", "SCHEDULE_NONE\n")) {
        return false;
    }
    if (scheduler.waitingJobCount() != 2 || scheduler.runningJobCount() != 0) {
        std::cerr << "FAIL: scheduling without Workers changed queued Jobs\n";
        return false;
    }

    if (!expectResponse(scheduler, "REGISTER_WORKER 1 2 4096", "REGISTER_OK\n") ||
        !expectState(scheduler, 2, 0, 2, 4096) ||
        !expectResponse(scheduler, "SCHEDULE_NEXT", "SCHEDULE_OK 101 1\n") ||
        !expectState(scheduler, 1, 1, 0, 0) ||
        !expectResponse(scheduler, "SCHEDULE_NEXT", "SCHEDULE_NONE\n") ||
        !expectState(scheduler, 1, 1, 0, 0) ||
        !expectResponse(scheduler, "COMPLETE_JOB 101", "COMPLETE_OK\n") ||
        !expectState(scheduler, 1, 0, 2, 4096) ||
        !expectResponse(scheduler, "SCHEDULE_NEXT", "SCHEDULE_OK 102 1\n") ||
        !expectState(scheduler, 0, 1, 1, 3072)) {
        return false;
    }

    if (!expectResponse(scheduler, "COMPLETE_JOB 101",
                        "COMPLETE_ERROR COMPLETION_REJECTED\n") ||
        !expectState(scheduler, 0, 1, 1, 3072) ||
        !expectResponse(scheduler, "COMPLETE_JOB 102", "COMPLETE_OK\n") ||
        !expectState(scheduler, 0, 0, 2, 4096) ||
        !expectResponse(scheduler, "SCHEDULE_NEXT", "SCHEDULE_NONE\n") ||
        !expectResponse(scheduler, "SUBMIT_JOB 101 1 1024",
                        "SUBMIT_ERROR JOB_REJECTED\n")) {
        return false;
    }

    // 完成后的资源足够再次分配一个占满 Worker 的 Job。
    return expectResponse(scheduler, "SUBMIT_JOB 103 2 4096", "SUBMIT_OK\n") &&
           expectResponse(scheduler, "SCHEDULE_NEXT", "SCHEDULE_OK 103 1\n") &&
           expectState(scheduler, 0, 1, 0, 0) &&
           expectResponse(scheduler, "COMPLETE_JOB 103", "COMPLETE_OK\n") &&
           expectState(scheduler, 0, 0, 2, 4096);
}

bool testMalformedRequestsDoNotMutateState() {
    Scheduler scheduler;
    if (!expectResponse(scheduler, "REGISTER_WORKER 1 2 4096", "REGISTER_OK\n") ||
        !expectResponse(scheduler, "SUBMIT_JOB 101 1 1024", "SUBMIT_OK\n")) {
        return false;
    }

    const std::vector<std::pair<std::string, std::string>> requests = {
        {"SCHEDULE_NEXT extra", "SCHEDULE_ERROR INVALID_MESSAGE\n"},
        {"SCHEDULE_NEXT 1", "SCHEDULE_ERROR INVALID_MESSAGE\n"},
        {"SCHEDULE_NEXT_EXTRA", "REQUEST_ERROR UNKNOWN_COMMAND\n"},
        {"COMPLETE_JOB", "COMPLETE_ERROR INVALID_MESSAGE\n"},
        {"COMPLETE_JOB 101 extra", "COMPLETE_ERROR INVALID_MESSAGE\n"},
        {"COMPLETE_JOB -1", "COMPLETE_ERROR INVALID_MESSAGE\n"},
        {"COMPLETE_JOB 101abc", "COMPLETE_ERROR INVALID_MESSAGE\n"},
        {"COMPLETE_JOB 999999999999999999999", "COMPLETE_ERROR INVALID_MESSAGE\n"},
        {"COMPLETE_JOB 101", "COMPLETE_ERROR COMPLETION_REJECTED\n"},
        {"COMPLETE_JOB 999", "COMPLETE_ERROR COMPLETION_REJECTED\n"},
        {"REGISTER_WORKER 1 2 4096", "REGISTER_ERROR WORKER_REJECTED\n"},
        {"REGISTER_WORKER 2 0 4096", "REGISTER_ERROR INVALID_MESSAGE\n"},
        {"SUBMIT_JOB 101 1 1024", "SUBMIT_ERROR JOB_REJECTED\n"},
        {"SUBMIT_JOB 102 0 1024", "SUBMIT_ERROR INVALID_MESSAGE\n"},
        {"SUBMIT_JOB_EXTRA 102 1 1024", "REQUEST_ERROR UNKNOWN_COMMAND\n"},
        {"", "REQUEST_ERROR UNKNOWN_COMMAND\n"},
        {"HELLO", "REQUEST_ERROR UNKNOWN_COMMAND\n"},
    };
    for (const auto& entry : requests) {
        if (!expectResponse(scheduler, entry.first, entry.second) ||
            !expectState(scheduler, 1, 0, 2, 4096)) {
            return false;
        }
    }

    // 非法完成消息不能释放正在运行的 Job。
    return expectResponse(scheduler, "SCHEDULE_NEXT", "SCHEDULE_OK 101 1\n") &&
           expectResponse(scheduler, "COMPLETE_JOB 101 extra",
                          "COMPLETE_ERROR INVALID_MESSAGE\n") &&
           expectState(scheduler, 0, 1, 1, 3072);
}

bool testStrictFifoIsPreserved() {
    Scheduler scheduler;
    return expectResponse(scheduler, "REGISTER_WORKER 1 2 4096", "REGISTER_OK\n") &&
           expectResponse(scheduler, "SUBMIT_JOB 201 3 1024", "SUBMIT_OK\n") &&
           expectResponse(scheduler, "SUBMIT_JOB 202 1 1024", "SUBMIT_OK\n") &&
           expectResponse(scheduler, "SCHEDULE_NEXT", "SCHEDULE_NONE\n") &&
           expectState(scheduler, 2, 0, 2, 4096);
}

bool testScheduleResponseParsing() {
    const auto normal = parseScheduleResult("SCHEDULE_OK 101 1");
    const auto zero = parseScheduleResult(" \tSCHEDULE_OK 0 0 \t");
    const std::string maximum = std::to_string(std::numeric_limits<int>::max());
    const auto boundary = parseScheduleResult("SCHEDULE_OK " + maximum + " " + maximum);
    if (!normal || normal->jobId != 101 || normal->workerId != 1 ||
        !zero || zero->jobId != 0 || zero->workerId != 0 ||
        !boundary || boundary->jobId != std::numeric_limits<int>::max() ||
        boundary->workerId != std::numeric_limits<int>::max()) {
        std::cerr << "FAIL: valid schedule response rejected\n";
        return false;
    }

    const std::vector<std::string> invalidMessages = {
        "", "SCHEDULE_OK", "SCHEDULE_OK 101", "SCHEDULE_OK 101 1 extra",
        "SCHEDULE_OK -1 1", "SCHEDULE_OK 101 -1", "SCHEDULE_OK abc 1",
        "SCHEDULE_OK 101 abc", "SCHEDULE_OK 101 1abc", "SCHEDULE_OK 101.5 1",
        "SCHEDULE_OK 101+1", "SCHEDULE_OK 101 999999999999999999999",
        "SCHEDULE_OK 999999999999999999999 1", "SCHEDULE_NONE",
        "COMPLETE_OK", "schedule_ok 101 1", "SCHEDULE_OK_EXTRA 101 1",
    };
    for (const auto& message : invalidMessages) {
        if (parseScheduleResult(message)) {
            std::cerr << "FAIL: invalid schedule response accepted: " << message << '\n';
            return false;
        }
    }
    return true;
}

}  // namespace

int main() {
    const struct TestCase {
        const char* name;
        bool (*run)();
    } tests[] = {
        {"scheduling and completion lifecycle", testSchedulingAndCompletionLifecycle},
        {"invalid requests preserve state", testMalformedRequestsDoNotMutateState},
        {"strict FIFO over the protocol", testStrictFifoIsPreserved},
        {"schedule response parsing", testScheduleResponseParsing},
    };

    for (const auto& test : tests) {
        if (!test.run()) {
            std::cerr << "FAIL: " << test.name << '\n';
            return 1;
        }
        std::cout << "PASS: " << test.name << '\n';
    }
    return 0;
}
