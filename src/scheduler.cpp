#include "scheduler.h"

Scheduler::Scheduler(SchedulingPolicy policy)
    : policy_(policy) {
}

void Scheduler::addWorker(const Worker& worker) {
    workers_.push_back(worker);
}

std::optional<int> Scheduler::schedule(const Job& job) {
    const auto workerIndex = selectWorker(job);

    if (workerIndex == std::nullopt) {
        return std::nullopt;
    }

    Worker& worker = workers_[workerIndex.value()];

    if (!worker.allocate(job)) {
        return std::nullopt;
    }

    return worker.getId();
}

std::size_t Scheduler::workerCount() const {
    return workers_.size();
}

void Scheduler::submitJob(const Job& job) {
    waitingJobs_.push(job);
}

std::size_t Scheduler::waitingJobCount() const {
    return waitingJobs_.size();
}

std::optional<ScheduleResult> Scheduler::scheduleNext() {
    if (waitingJobs_.empty()) {
        return std::nullopt;
    }

    const Job& job = waitingJobs_.front();
    const auto workerId = schedule(job);

    if (workerId == std::nullopt) {
        return std::nullopt;
    }

    const int jobId = job.jobId;

    waitingJobs_.pop();

    return ScheduleResult{jobId, workerId.value()};
}

std::optional<std::size_t> Scheduler::findFirstFitWorker(const Job& job) const {
    for (std::size_t index = 0; index < workers_.size(); index++) {
        if (workers_[index].canRun(job)) {
            return index;
        }
    }

    return std::nullopt;
}

std::optional<std::size_t> Scheduler::findBinPackWorker(const Job& job) const {
    std::optional<std::size_t> bestIndex;

    int bestRemainingCpu = 0;
    int bestRemainingMemory = 0;

    for (std::size_t index = 0; index < workers_.size(); ++index) {
        const Worker& worker = workers_[index];

        // 不能运行则 continue
        if (!worker.canRun(job)) {
            continue;
        }

        const Resource& available = worker.getAvailableResource();

        // 计算放入 Job 后的剩余 CPU 和内存
        const int remainingCpu = available.cpuCores - job.requiredResource.cpuCores;
        const int remainingMemory = available.memoryMB - job.requiredResource.memoryMB;

        // 如果还没有候选节点，保存当前节点
        //
        // 或当前 remainingCpu 更小，更新候选节点
        //
        // 或 CPU 相同但 remainingMemory 更小，更新候选节点
        if (!bestIndex || remainingCpu < bestRemainingCpu || (remainingCpu == bestRemainingCpu && remainingMemory < bestRemainingMemory)) {
            bestIndex = index;
            bestRemainingCpu = remainingCpu;
            bestRemainingMemory = remainingMemory;
        }
    }

    return bestIndex;
}

std::optional<std::size_t> Scheduler::selectWorker(const Job& job) const {
    switch (policy_) {
        case SchedulingPolicy::FirstFit:
            return findFirstFitWorker(job);

        case SchedulingPolicy::BinPack:
            return findBinPackWorker(job);
    }

    return std::nullopt;
}
