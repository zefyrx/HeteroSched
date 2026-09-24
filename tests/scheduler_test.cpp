#include <iostream>

#include "job.h"
#include "scheduler.h"
#include "worker.h"

bool testBinPackChoosesTighterWorker() {
    Scheduler scheduler{SchedulingPolicy::BinPack};

    scheduler.addWorker(Worker{1, {8, 16384}});
    scheduler.addWorker(Worker{2, {4, 8192}});

    const Job job{1, {2, 4096}};

    scheduler.submitJob(job);
    const auto result = scheduler.scheduleNext();

    if (!result) {
        std::cerr << "FAIL: BinPack did not schedule the job\n";
        return false;
    }

    if (result->jobId != job.jobId) {
        std::cerr << "FAIL: scheduled the wrong job\n";
        return false;
    }

    if (result->workerId != 2) {
        std::cerr << "FAIL: BinPack selected the wrong worker\n";
        return false;
    }

    return true;
}

bool testFirstFitChoosesFirstSuitableWorker() {
    Scheduler scheduler{SchedulingPolicy::FirstFit};

    scheduler.addWorker(Worker{1, {4, 8192}});
    scheduler.addWorker(Worker{2, {8, 16384}});

    const Job job{1, {2, 4096}};

    scheduler.submitJob(job);
    const auto result = scheduler.scheduleNext();

    if (!result) {
        std::cerr << "FAIL: FirstFit did not schedule the job\n";
        return false;
    }

    if (result->jobId != job.jobId) {
        std::cerr << "FAIL: scheduled the wrong job\n";
        return false;
    }

    if (result->workerId != 1) {
        std::cerr << "FAIL: FirstFit selected the wrong worker\n";
        return false;
    }

    return true;
}

bool testLeastAllocatedChoosesLowerUtilizationWorker() {
    Scheduler scheduler{SchedulingPolicy::LeastAllocated};

    scheduler.addWorker(Worker{1, {4, 8192}});
    scheduler.addWorker(Worker{2, {8, 16384}});

    const Job job{1, {2, 4096}};

    scheduler.submitJob(job);
    const auto result = scheduler.scheduleNext();

    if (!result) {
        std::cerr << "FAIL: LeastAllocated did not schedule the job\n";
        return false;
    }

    if (result->jobId != job.jobId) {
        std::cerr << "FAIL: scheduled the wrong job\n";
        return false;
    }

    if (result->workerId != 2) {
        std::cerr << "FAIL: LeastAllocated selected the wrong worker\n";
        return false;
    }

    return true;
}

bool testUnschedulableJobRemainsQueued() {
    Scheduler scheduler{SchedulingPolicy::FirstFit};

    scheduler.addWorker(Worker{1, {2, 2048}});

    Job job{1, {4, 4096}};

    scheduler.submitJob(job);
    const auto result1 = scheduler.scheduleNext();

    if (result1) {
        std::cerr << "FAIL: Scheduler should not schedule the job\n";
        return false;
    }

    // 第一次调度失败后
    if (scheduler.waitingJobCount() != 1 || scheduler.runningJobCount() != 0) {
        std::cerr << "FAIL: unschedulable job was not kept waiting\n";
        return false;
    }

    scheduler.addWorker(Worker{2, {4, 4096}});
    const auto result2 = scheduler.scheduleNext();

    if (!result2) {
        std::cerr << "FAIL: Scheduler did not schedule the job\n";
        return false;
    }

    if (result2->jobId != job.jobId) {
        std::cerr << "FAIL: scheduled the wrong job\n";
        return false;
    }

    if (result2->workerId != 2) {
        std::cerr << "FAIL: Scheduler selected the wrong worker\n";
        return false;
    }

    // 第二次调度成功后
    if (scheduler.waitingJobCount() != 0 ||
        scheduler.runningJobCount() != 1) {
        std::cerr << "FAIL: queue counts are incorrect after scheduling\n";
        return false;
    }

    return true;
}

int main() {
    if (!testBinPackChoosesTighterWorker()) {
        return 1;
    }

    if (!testFirstFitChoosesFirstSuitableWorker()) {
        return 1;
    }

    if (!testLeastAllocatedChoosesLowerUtilizationWorker()) {
        return 1;
    }

    if (!testUnschedulableJobRemainsQueued()) {
        return 1;
    }

    std::cout << "PASS: all scheduler tests passed\n";
    return 0;
}