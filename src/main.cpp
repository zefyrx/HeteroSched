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

    const auto workerId1 = scheduler.schedule(job1);
    if (workerId1) {
        std::cout << "Job " << job1.jobId
                  << " assigned to Worker " << *workerId1 << '\n';
    } else {
        std::cout << "Job " << job1.jobId
                  << " could not be scheduled\n";
    }

    const auto workerId2 = scheduler.schedule(job2);
    if (workerId2) {
        std::cout << "Job " << job2.jobId
                  << " assigned to Worker " << *workerId2 << '\n';
    } else {
        std::cout << "Job " << job2.jobId
                  << " could not be scheduled\n";
    }

    return 0;
}