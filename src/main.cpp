#include <iostream>

#include "job.h"
#include "scheduler.h"
#include "worker.h"

int main() {
    Scheduler firstFitScheduler{SchedulingPolicy::FirstFit};
    Scheduler binPackScheduler{SchedulingPolicy::BinPack};

    firstFitScheduler.addWorker(Worker{1, {8, 16384}});
    firstFitScheduler.addWorker(Worker{2, {4, 8192}});

    binPackScheduler.addWorker(Worker{1, {8, 16384}});
    binPackScheduler.addWorker(Worker{2, {4, 8192}});

    Job job{1, {2, 4096}};

    firstFitScheduler.submitJob(job);
    binPackScheduler.submitJob(job);

    const auto firstFitResult = firstFitScheduler.scheduleNext();

    const auto binPackResult = binPackScheduler.scheduleNext();

    std::cout << "FirstFit assigned Job 1 to Worker " << firstFitResult.value().workerId << "\n";
    std::cout << "BinPack assigned Job 1 to Worker " << binPackResult.value().workerId << "\n";

    return 0;
}
