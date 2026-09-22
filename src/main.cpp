#include <iostream>

#include "job.h"
#include "scheduler.h"
#include "worker.h"

int main() {
    Scheduler scheduler;

    scheduler.addWorker(Worker{1, {8, 16384}});
    scheduler.addWorker(Worker{2, {4, 8192}});

    Job job1{1, {6, 12288}};
    Job job2{2, {3, 4096}};
    Job job3{3, {10, 1024}};
    Job job4{4, {1, 1024}};

    scheduler.submitJob(job1);
    scheduler.submitJob(job2);
    scheduler.submitJob(job3);
    scheduler.submitJob(job4);

    std::cout << "Waiting jobs before scheduling: "
          << scheduler.waitingJobCount() << '\n';

    while (const auto result = scheduler.scheduleNext()) {
        std::cout << "Job " << result->jobId
                << " assigned to Worker " << result->workerId << '\n';
    }

    std::cout << "Waiting jobs after scheduling: "
            << scheduler.waitingJobCount() << '\n';

    return 0;
}
