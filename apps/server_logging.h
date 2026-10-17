#ifndef HETEROSCHED_SERVER_LOGGING_H
#define HETEROSCHED_SERVER_LOGGING_H

#include <iostream>
#include <string>

#include "core/scheduler.h"

inline void logSchedulerRequest(const std::string& request, const std::string& response,
                                const Scheduler& scheduler) {
    std::cout << "Received: " << request << '\n'
              << "Response: " << response
              << "State: workers=" << scheduler.workerCount()
              << ", waiting=" << scheduler.waitingJobCount()
              << ", running=" << scheduler.runningJobCount() << '\n';
    for (const auto& worker : scheduler.getWorkerSnapshots()) {
        std::cout << "Worker " << worker.workerId
                  << ": available CPU=" << worker.availableResource.cpuCores
                  << ", memoryMB=" << worker.availableResource.memoryMB << '\n';
    }
    std::cout << std::flush;
}

#endif
