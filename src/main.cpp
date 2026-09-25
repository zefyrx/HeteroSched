#include <iostream>
#include <chrono>
#include <thread>

#include "job.h"
#include "scheduler.h"
#include "worker.h"

void executeJob(int workerId, int jobId, int durationMs) {
    std::cout << "Worker " << workerId
              << " started Job " << jobId << '\n';

    std::this_thread::sleep_for(
        std::chrono::milliseconds(durationMs));

    std::cout << "Worker " << workerId
              << " finished Job " << jobId << '\n';
}

int main() {
    Scheduler scheduler{SchedulingPolicy::FirstFit};
    scheduler.addWorker(Worker{1, {2, 4096}});
    scheduler.addWorker(Worker{2, {2, 4096}});

    const Job job1{1, {2, 4096}};
    const Job job2{2, {2, 4096}};

    if (!scheduler.submitJob(job1)) {
        std::cerr << "Failed to submit Job " << job1.jobId << '\n';
        return 1;
    }

    if (!scheduler.submitJob(job2)) {
        std::cerr << "Failed to submit Job " << job2.jobId << '\n';
        return 1;
    }

    const auto result1 = scheduler.scheduleNext();
    
    if (!result1) {
        std::cerr << "Failed to schedule Job " << job1.jobId << '\n';
        return 1;
    }

    const auto result2 = scheduler.scheduleNext();

    if (!result2) {
        std::cerr << "Failed to schedule Job " << job2.jobId << '\n';
        return 1;
    }

    std::thread workerThread1(executeJob, result1->workerId, result1->jobId, 700);
    std::thread workerThread2(executeJob, result2->workerId, result2->jobId, 300);

    // 调用 workerThread.join()，等待模拟执行线程结束。
    // completeJob() 必须在 join() 返回之后调用。
    workerThread1.join();
    workerThread2.join();

    if (!scheduler.completeJob(result1->jobId)) {
        std::cerr << "Failed to complete Job " << result1->jobId << '\n';
        return 1;
    }

    if (!scheduler.completeJob(result2->jobId)) {
        std::cerr << "Failed to complete Job " << result2->jobId << '\n';
        return 1;
    }

    std::cout << "Completed Job " << result1->jobId << " and " << result2->jobId << " and released its resources\n";

    return 0;
}
