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

    scheduler.submitJob(job1);
    scheduler.submitJob(job2);
    scheduler.submitJob(job3);

    std::cout << "Waiting jobs: "
            << scheduler.waitingJobCount() << '\n';

    return 0;
}
