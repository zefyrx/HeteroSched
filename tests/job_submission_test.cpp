#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "job_submission.h"
#include "scheduler.h"
#include "worker.h"

namespace {

bool testNormalSubmission() {
    const auto job = parseJobSubmission("SUBMIT_JOB 101 2 4096");
    if (!job || job->jobId != 101 ||
        job->requiredResource.cpuCores != 2 ||
        job->requiredResource.memoryMB != 4096) {
        std::cerr << "FAIL: normal submission has incorrect Job data\n";
        return false;
    }
    return true;
}

bool testAllowsZeroIdAndWhitespace() {
    const auto job = parseJobSubmission(" \tSUBMIT_JOB\t0  1 1024 \t");
    if (!job || job->jobId != 0 ||
        job->requiredResource.cpuCores != 1 ||
        job->requiredResource.memoryMB != 1024) {
        std::cerr << "FAIL: valid zero ID or whitespace was rejected\n";
        return false;
    }
    return true;
}

bool testAllowsIntMaximum() {
    const int maximum = std::numeric_limits<int>::max();
    const std::string field = std::to_string(maximum);
    const auto job = parseJobSubmission(
        "SUBMIT_JOB " + field + " " + field + " " + field);
    if (!job || job->jobId != maximum ||
        job->requiredResource.cpuCores != maximum ||
        job->requiredResource.memoryMB != maximum) {
        std::cerr << "FAIL: valid int upper boundary was rejected\n";
        return false;
    }
    return true;
}

bool testAllowsSignedAdjacentFields() {
    // 沿用注册解析器的整数提取语义，相邻的带正号数字也能提取。
    const auto job = parseJobSubmission("SUBMIT_JOB 101+2+4096");
    if (!job || job->jobId != 101 ||
        job->requiredResource.cpuCores != 2 ||
        job->requiredResource.memoryMB != 4096) {
        std::cerr << "FAIL: signed adjacent fields have incorrect data\n";
        return false;
    }
    return true;
}

bool testRejectsInvalidSubmissions() {
    const std::vector<std::string> invalidMessages = {
        "",
        "   \t",
        "REGISTER_WORKER 101 2 4096",
        "SUBMIT_JOB_EXTRA 101 2 4096",
        "submit_job 101 2 4096",
        "SUBMIT_JOB",
        "SUBMIT_JOB 101",
        "SUBMIT_JOB 101 2",
        "SUBMIT_JOB 101 2 4096 extra",
        "SUBMIT_JOB -1 2 4096",
        "SUBMIT_JOB 101 0 4096",
        "SUBMIT_JOB 101 -2 4096",
        "SUBMIT_JOB 101 2 0",
        "SUBMIT_JOB 101 2 -4096",
        "SUBMIT_JOB abc 2 4096",
        "SUBMIT_JOB 101 two 4096",
        "SUBMIT_JOB 101 2 memory",
        "SUBMIT_JOB 101abc 2 4096",
        "SUBMIT_JOB 101 2abc 4096",
        "SUBMIT_JOB 101 2 4096abc",
        "SUBMIT_JOB 101 2 4096.5",
        "SUBMIT_JOB 999999999999999999999 2 4096",
        "SUBMIT_JOB 101 999999999999999999999 4096",
        "SUBMIT_JOB 101 2 999999999999999999999",
    };

    for (const auto& message : invalidMessages) {
        if (parseJobSubmission(message)) {
            std::cerr << "FAIL: invalid submission accepted: "
                      << message << '\n';
            return false;
        }
    }
    return true;
}

bool testParsedJobsEnterQueueInOrder() {
    Scheduler scheduler;
    const auto first = parseJobSubmission("SUBMIT_JOB 101 2 4096");
    const auto duplicate = parseJobSubmission("SUBMIT_JOB 101 4 8192");
    const auto second = parseJobSubmission("SUBMIT_JOB 102 1 2048");
    if (!first || !duplicate || !second) {
        std::cerr << "FAIL: could not parse queue test inputs\n";
        return false;
    }

    // 无 Worker 时也可以提交；入队本身不表示已分配或开始运行。
    if (!scheduler.submitJob(*first) || scheduler.workerCount() != 0 ||
        scheduler.waitingJobCount() != 1 || scheduler.runningJobCount() != 0) {
        std::cerr << "FAIL: parsed Job could not wait without a Worker\n";
        return false;
    }

    if (scheduler.submitJob(*duplicate) || scheduler.waitingJobCount() != 1) {
        std::cerr << "FAIL: duplicate Job changed the waiting queue\n";
        return false;
    }

    // Worker ID 与已提交的 Job ID 可以相同。
    if (!scheduler.addWorker(Worker{101, {8, 16384}}) ||
        !scheduler.submitJob(*second) || scheduler.waitingJobCount() != 2 ||
        scheduler.runningJobCount() != 0) {
        std::cerr << "FAIL: independent IDs or second Job submission failed\n";
        return false;
    }

    const auto snapshots = scheduler.getWorkerSnapshots();
    if (snapshots.size() != 1 ||
        snapshots[0].availableResource.cpuCores != 8 ||
        snapshots[0].availableResource.memoryMB != 16384) {
        std::cerr << "FAIL: submitting Jobs allocated Worker resources\n";
        return false;
    }

    const auto firstResult = scheduler.scheduleNext();
    const auto secondResult = scheduler.scheduleNext();
    if (!firstResult || !secondResult ||
        firstResult->jobId != 101 || secondResult->jobId != 102 ||
        firstResult->workerId != 101 || secondResult->workerId != 101 ||
        scheduler.waitingJobCount() != 0 || scheduler.runningJobCount() != 2) {
        std::cerr << "FAIL: parsed Jobs did not preserve FIFO order\n";
        return false;
    }
    return true;
}

}  // namespace

int main() {
    const struct TestCase {
        const char* name;
        bool (*run)();
    } tests[] = {
        {"normal Job submission", testNormalSubmission},
        {"zero ID and whitespace", testAllowsZeroIdAndWhitespace},
        {"int upper boundary", testAllowsIntMaximum},
        {"signed adjacent fields", testAllowsSignedAdjacentFields},
        {"invalid submissions", testRejectsInvalidSubmissions},
        {"parsed Jobs and FIFO queue", testParsedJobsEnterQueueInOrder},
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
