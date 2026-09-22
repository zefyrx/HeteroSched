#include "scheduler.h"

void Scheduler::addWorker(const Worker& worker) {
    workers_.push_back(worker);
}

std::optional<int> Scheduler::schedule(const Job& job) {
    if (workers_.empty()) {
        return std::nullopt;
    }

    for (auto& w : workers_) {          // 要用引用传递
        if (w.allocate(job)) {
            return w.getId();
        }
    }

    return std::nullopt;
}

std::size_t Scheduler::workerCount() const {
    return workers_.size();
}
