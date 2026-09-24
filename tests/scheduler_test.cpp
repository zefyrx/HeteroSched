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

    // 1. 检查 result 是否有值
    // 2. 检查 jobId 是否为 1
    // 3. 检查 workerId 是否为 2
    //
    // 任一条件不满足时打印错误并返回 false。
    if (!result || result->jobId != 1 || result->workerId != 2) {
        return false;
    }

    return true;
}

int main() {
    if (!testBinPackChoosesTighterWorker()) {
        return 1;
    }

    std::cout << "PASS: BinPack chooses the tighter worker\n";
    return 0;
}