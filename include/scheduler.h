#ifndef HETEROSCHED_SCHEDULER_H
#define HETEROSCHED_SCHEDULER_H

#include <cstddef>
#include <optional>
#include <vector>

#include "job.h"
#include "worker.h"

class Scheduler {
public:
    void addWorker(const Worker& worker);
    std::optional<int> schedule(const Job& job);
    std::size_t workerCount() const;

private:
    std::vector<Worker> workers_;
};

#endif
