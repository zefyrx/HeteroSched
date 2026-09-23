#include "scheduler.h"

void Scheduler::addWorker(const Worker& worker) {
    workers_.push_back(worker);
}

std::optional<int> Scheduler::schedule(const Job& job) {
    const auto workerIndex = findFirstFitWorker(job);

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