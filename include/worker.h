#ifndef HETEROSCHED_WORKER_H
#define HETEROSCHED_WORKER_H

#include "job.h"
#include "resource.h"

class Worker {
public:
    Worker(int workerId, const Resource& totalResource);

    int getId() const;

    bool canRun(const Job& job) const;
    bool allocate(const Job& job);
    bool release(const Job& job);
    const Resource& getAvailableResource() const;

private:
    int workerId_;
    Resource totalResource_;
    Resource availableResource_;
};

#endif
