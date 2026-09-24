#include <iostream>

#include "job.h"
#include "scheduler.h"
#include "worker.h"

int main() {
    Scheduler scheduler{SchedulingPolicy::FirstFit};
    scheduler.addWorker(Worker{1, {4, 8192}});

    const Job job1{1, {3, 4096}};
    const Job job2{2, {2, 4096}};

    scheduler.submitJob(job1);
    scheduler.submitJob(job2);

    const auto job1Result = scheduler.scheduleNext();
    if (!job1Result || job1Result->jobId != 1 ||
        job1Result->workerId != 1) {
        std::cerr << "FAIL: Job 1 was not assigned to Worker 1\n";
        return 1;
    }

    if (scheduler.runningJobCount() != 1 ||
        scheduler.waitingJobCount() != 1) {
        std::cerr << "FAIL: incorrect counts after scheduling Job 1\n";
        return 1;
    }

    const auto job2BeforeCompletion = scheduler.scheduleNext();
    if (job2BeforeCompletion) {
        std::cerr << "FAIL: Job 2 ran before Job 1 released resources\n";
        return 1;
    }

    if (scheduler.completeJob(999)) {
        std::cerr << "FAIL: completing an unknown Job should fail\n";
        return 1;
    }

    if (!scheduler.completeJob(1)) {
        std::cerr << "FAIL: Job 1 could not be completed\n";
        return 1;
    }

    if (scheduler.completeJob(1)) {
        std::cerr << "FAIL: Job 1 was completed twice\n";
        return 1;
    }

    if (scheduler.runningJobCount() != 0 ||
        scheduler.waitingJobCount() != 1) {
        std::cerr << "FAIL: incorrect counts after completing Job 1\n";
        return 1;
    }

    const auto job2AfterCompletion = scheduler.scheduleNext();
    if (!job2AfterCompletion || job2AfterCompletion->jobId != 2 ||
        job2AfterCompletion->workerId != 1) {
        std::cerr << "FAIL: Job 2 did not run after resources were released\n";
        return 1;
    }

    if (scheduler.runningJobCount() != 1 ||
        scheduler.waitingJobCount() != 0) {
        std::cerr << "FAIL: incorrect final running/waiting counts\n";
        return 1;
    }

    if (!scheduler.completeJob(2) || scheduler.runningJobCount() != 0) {
        std::cerr << "FAIL: Job 2 did not complete cleanly\n";
        return 1;
    }

    std::cout << "PASS: job lifecycle and resource release work correctly\n";

    return 0;
}
