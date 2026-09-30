#include "scheduler.h"
#include "resource.h"

Scheduler::Scheduler(SchedulingPolicy policy)
    : policy_(policy) {
}

bool Scheduler::addWorker(const Worker& worker) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!(hasPositiveResources(worker.getTotalResource()) && worker.getId() >= 0)) {
        return false;
    }

    if (!workerIds_.insert(worker.getId()).second) {
        return false;
    }

    workers_.push_back(worker);

    return true;
}

std::optional<std::size_t> Scheduler::schedule(const Job& job) {
    const auto workerIndex = selectWorker(job);

    if (workerIndex == std::nullopt) {
        return std::nullopt;
    }

    Worker& worker = workers_[workerIndex.value()];

    if (!worker.allocate(job)) {
        return std::nullopt;
    }

    return workerIndex;
}

std::size_t Scheduler::workerCount() const {
    std::lock_guard<std::mutex> lock(mutex_);

    return workers_.size();
}

bool Scheduler::submitJob(const Job& job) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!(hasPositiveResources(job.requiredResource) && job.jobId >= 0)) {
        return false;
    }

    if (!submittedJobIds_.insert(job.jobId).second) {
        return false;
    }

    waitingJobs_.push(job);

    return true;
}

std::size_t Scheduler::waitingJobCount() const {
    std::lock_guard<std::mutex> lock(mutex_);

    return waitingJobs_.size();
}

std::optional<ScheduleResult> Scheduler::scheduleNext() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (waitingJobs_.empty()) {
        return std::nullopt;
    }

    const Job& job = waitingJobs_.front();
    const auto workerIndex = schedule(job);

    if (!workerIndex) {
        return std::nullopt;
    }

    const int jobId = job.jobId;
    const int workerId = workers_[workerIndex.value()].getId();

    RunningJob runningJob{job, workerIndex.value()};
    runningJobs_.emplace(jobId, runningJob);

    waitingJobs_.pop();

    return ScheduleResult{jobId, workerId};
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

std::optional<std::size_t> Scheduler::findLeastAllocatedWorker(const Job& job) const {
    std::optional<std::size_t> bestIndex;
    double bestScore = 0.0;

    for (std::size_t index = 0; index < workers_.size(); ++index) {
        const Worker& worker = workers_[index];

        if (!worker.canRun(job)) {
            continue;
        }

        const Resource& total = worker.getTotalResource();

        const Resource& available = worker.getAvailableResource();

        // 计算加入 job 后已使用的 CPU
        // total - available + job required
        const int usedCpuAfterAllocation = total.cpuCores - available.cpuCores + job.requiredResource.cpuCores;

        // 计算加入 job 后已使用的内存
        const int usedMemoryAfterAllocation = total.memoryMB - available.memoryMB + job.requiredResource.memoryMB;

        // 转换为 double，计算两个利用率
        const double cpuUtilization = static_cast<double>(usedCpuAfterAllocation) / total.cpuCores;
        const double memoryUtilization = static_cast<double>(usedMemoryAfterAllocation) / total.memoryMB;

        // 两个利用率相加得到 score
        const double score = cpuUtilization + memoryUtilization;

        // 尚无候选，或 score 更小时更新最佳候选
        if (!bestIndex || score < bestScore) {
            bestIndex = index;
            bestScore = score;
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

        case SchedulingPolicy::LeastAllocated:
            return findLeastAllocatedWorker(job);
    }

    return std::nullopt;
}

// Scheduler 收到通知，某个正在运行的任务已经执行完毕，负责处理任务执行结束后的状态和资源回收
bool Scheduler::completeJob(int jobId) {
    std::lock_guard<std::mutex> lock(mutex_);

    const auto it = runningJobs_.find(jobId);

    // 没有这个运行任务时返回 false
    if (it == runningJobs_.end()) {
        return false;
    }

    RunningJob& runningJob = it->second;
    Worker& worker = workers_[runningJob.workerIndex];

    // 释放资源，失败则返回 false
    if (!worker.release(runningJob.job)) {
        return false;
    }

    // 释放成功后从 runningJobs_ 删除记录
    runningJobs_.erase(jobId);

    return true;
}

std::size_t Scheduler::runningJobCount() const {
    std::lock_guard<std::mutex> lock(mutex_);

    return runningJobs_.size();
}

std::vector<WorkerSnapshot> Scheduler::getWorkerSnapshots() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<WorkerSnapshot> snapshots;
    snapshots.reserve(workers_.size());

    for (const Worker& worker : workers_) {
        snapshots.push_back({
            worker.getId(),
            worker.getTotalResource(),
            worker.getAvailableResource()
        });
    }

    return snapshots;
}