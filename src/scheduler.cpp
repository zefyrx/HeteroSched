#include "scheduler.h"

void Scheduler::addWorker(const Worker& worker) {
    workers_.push_back(worker);
}

std::optional<int> Scheduler::schedule(const Job& job) {
    if (workers_.empty()) {
        return std::nullopt;
    }

    for (auto& w : workers_) {          // 使用引用遍历，修改容器中的原对象
        if (w.allocate(job)) {
            return w.getId();
        }
    }

    return std::nullopt;
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
