#ifndef HETEROSCHED_SCHEDULER_H
#define HETEROSCHED_SCHEDULER_H

#include <cstddef>
#include <optional>
#include <vector>
#include <queue>

#include "job.h"
#include "worker.h"

struct ScheduleResult {
    int jobId;
    int workerId;
};

class Scheduler {
public:
    void addWorker(const Worker& worker);
    void submitJob(const Job& job);

    std::optional<ScheduleResult> scheduleNext();

    std::size_t workerCount() const;
    std::size_t waitingJobCount() const;

private:
    std::vector<Worker> workers_;
    std::queue<Job> waitingJobs_;

    std::optional<int> schedule(const Job& job);
};

#endif
