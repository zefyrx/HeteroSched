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

    if (result->workerId != 2) {
        std::cerr << "FAIL: LeastAllocated selected the wrong worker\n";
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

    std::cout << "PASS: BinPack chooses the tighter worker\n";
    return 0;
}