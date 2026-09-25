#include <iostream>
#include <chrono>
#include <thread>

#include "job.h"
#include "scheduler.h"
#include "worker.h"

int main() {
    Scheduler scheduler{SchedulingPolicy::FirstFit};
    scheduler.addWorker(Worker{1, {4, 8192}});

    const Job job{1, {2, 4096}};

    if (!scheduler.submitJob(job)) {
        std::cerr << "Failed to submit Job " << job.jobId << '\n';
        return 1;
    }

    const auto result = scheduler.scheduleNext();
    if (!result) {
        std::cerr << "No Worker can run Job " << job.jobId << '\n';
        return 1;
    }

    const int jobId = result->jobId;
    std::cout << "Scheduled Job " << jobId
              << " to Worker " << result->workerId << '\n';

    // 创建一个名为 workerThread 的 std::thread，并按值捕获 jobId。
    // 在线程的 lambda 中完成以下操作：
    //   1. 输出 Worker 开始执行哪个 Job；
    //   2. 使用 std::this_thread::sleep_for 模拟执行 500 毫秒；
    //   3. 输出 Worker 已经完成哪个 Job。
    //
    // 注意：本次实验不要在线程中访问 scheduler。
    std::thread workerThread(
        [jobId] {
            std::cout << "Worker started Job " << jobId << '\n';

            std::this_thread::sleep_for(std::chrono::milliseconds(500));

            std::cout << "Worker finished Job " << jobId << '\n';
        }
    );

    // 调用 workerThread.join()，等待模拟执行线程结束。
    // completeJob() 必须在 join() 返回之后调用。
    workerThread.join();

    if (!scheduler.completeJob(jobId)) {
        std::cerr << "Failed to complete Job " << jobId << '\n';
        return 1;
    }

    std::cout << "Completed Job " << jobId
              << " and released its resources\n";

    return 0;
}
