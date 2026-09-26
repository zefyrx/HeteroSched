#include <iostream>
#include <thread>

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

bool testRejectsDuplicateJobId() {
    Scheduler scheduler{SchedulingPolicy::FirstFit};

    scheduler.addWorker(Worker{1, {4, 8192}});
    scheduler.addWorker(Worker{2, {8, 16384}});

    const Job job{1, {2, 4096}};

    if (!scheduler.submitJob(job)) {
        return false;
    }
    
    // 尝试再次提交该 job
    if (scheduler.submitJob(job)) {
        return false;
    }

    if (scheduler.waitingJobCount() != 1) {
        return false;
    }

    const auto result = scheduler.scheduleNext();

    if (!result) {
        return false;
    }

    if (!scheduler.completeJob(result->jobId)) {
        return false;
    }

    if (scheduler.waitingJobCount() != 0) {
        return false;
    }

    if (scheduler.submitJob(job)) {
        return false;
    }

    if (scheduler.waitingJobCount() != 0) {
        return false;
    }

    return true;
}

bool testConcurrentJobCompletion() {
    Scheduler scheduler{SchedulingPolicy::FirstFit};

    scheduler.addWorker(Worker{1, {2, 4096}});
    scheduler.addWorker(Worker{2, {2, 4096}});

    const Job job1{1, {2, 4096}};
    const Job job2{2, {2, 4096}};

    if (!scheduler.submitJob(job1) || !scheduler.submitJob(job2)) {
        std::cerr << "FAIL: could not submit jobs for concurrent completion\n";
        return false;
    }

    const auto result1 = scheduler.scheduleNext();
    const auto result2 = scheduler.scheduleNext();

    if (!result1 || !result2) {
        std::cerr << "FAIL: could not schedule jobs for concurrent completion\n";
        return false;
    }

    if (scheduler.runningJobCount() != 2) {
        std::cerr << "FAIL: expected two running jobs before completion\n";
        return false;
    }

    bool completion1 = false;
    bool completion2 = false;

    std::thread completionThread1([&scheduler, &completion1, result1]() {
        completion1 = scheduler.completeJob(result1->jobId);
    });

    std::thread completionThread2([&scheduler, &completion2, result2]() {
        completion2 = scheduler.completeJob(result2->jobId);
    });

    completionThread1.join();
    completionThread2.join();

    if (!completion1 || !completion2) {
        std::cerr << "FAIL: concurrent job completion failed\n";
        return false;
    }

    if (scheduler.runningJobCount() != 0) {
        std::cerr << "FAIL: completed jobs remain in the running set\n";
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

    if (!testRejectsDuplicateJobId()) {
        return 1;
    }

    if (!testConcurrentJobCompletion()) {
        return 1;
    }

    std::cout << "PASS: all scheduler tests passed\n";
    return 0;
}
