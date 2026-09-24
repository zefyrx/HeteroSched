#include <iostream>

#include "job.h"
#include "scheduler.h"
#include "worker.h"

int main() {
    Scheduler firstFitScheduler{SchedulingPolicy::FirstFit};
    Scheduler binPackScheduler{SchedulingPolicy::BinPack};
    Scheduler leastAllocatedScheduler{SchedulingPolicy::LeastAllocated};

    firstFitScheduler.addWorker(Worker{1, {8, 16384}});
    firstFitScheduler.addWorker(Worker{2, {8, 16384}});

    binPackScheduler.addWorker(Worker{1, {8, 16384}});
    binPackScheduler.addWorker(Worker{2, {8, 16384}});

    leastAllocatedScheduler.addWorker(Worker{1, {8, 16384}});
    leastAllocatedScheduler.addWorker(Worker{2, {8, 16384}});

    Job job{0, {4, 8192}};

    firstFitScheduler.submitJob(job);
    binPackScheduler.submitJob(job);
    leastAllocatedScheduler.submitJob(job);

    Job targetJob{1, {2, 4096}};

    firstFitScheduler.submitJob(targetJob);
    binPackScheduler.submitJob(targetJob);
    leastAllocatedScheduler.submitJob(targetJob);
    
    firstFitScheduler.scheduleNext();
    binPackScheduler.scheduleNext();
    leastAllocatedScheduler.scheduleNext();

    const auto firstFitResult = firstFitScheduler.scheduleNext();

    const auto binPackResult = binPackScheduler.scheduleNext();

    const auto leastAllocatedResult = leastAllocatedScheduler.scheduleNext();

    std::cout << "FirstFit assigned targetJob to Worker " << firstFitResult->workerId << "\n";
    std::cout << "BinPack assigned targetJob to Worker " << binPackResult->workerId << "\n";
    std::cout << "LeastAllocated assigned targetJob to Worker " << leastAllocatedResult->workerId << "\n";

    return 0;
}
