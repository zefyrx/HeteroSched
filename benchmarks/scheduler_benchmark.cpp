#include <iostream>
#include <vector>
#include <random>
#include <iomanip>
#include <chrono>

#include "job.h"
#include "scheduler.h"

struct BenchmarkResult {
    std::size_t scheduleJobs;
    std::size_t waitingJobs;
    long long elapsedMicroseconds;
};

std::vector<Job> generateJobs(std::size_t count, unsigned int seed) {
    std::vector<Job> jobs;
    jobs.reserve(count);

    std::mt19937 generator(seed);

    // 随机生成 CPU 核数：1 ~ 4
    std::uniform_int_distribution<int> cpuDist(1, 4);

    // 随机生成内存：1024 ~ 8192 MB
    std::uniform_int_distribution<int> memoryDist(1024, 8192);

    for (std::size_t i = 0; i < count; ++i) {
        Job job;

        job.jobId = static_cast<int>(i);

        job.requiredResource.cpuCores = cpuDist(generator);
        job.requiredResource.memoryMB = memoryDist(generator);

        jobs.push_back(job);
    }

    return jobs;
}

BenchmarkResult runBenchmark(SchedulingPolicy policy, const std::vector<Job>& jobs) {

    // 每种策略都重新创建一个 Scheduler
    Scheduler scheduler(policy);

    // 添加完全相同的 Worker
    scheduler.addWorker(Worker{1, {8, 16384}});

    scheduler.addWorker(Worker{2, {4, 8192}});

    scheduler.addWorker(Worker{3, {16, 32768}});

    // 先提交所有 Job
    for (const auto& job : jobs) {
        if (!scheduler.submitJob(job)) {
            std::cerr << "Failed to submit Job " << job.jobId << '\n';
        }
    }

    std::size_t scheduledJobs = 0;

    // 从这里才开始计时
    auto start = std::chrono::steady_clock::now();

    while (scheduler.scheduleNext()) {
        ++scheduledJobs;
    }

    auto end = std::chrono::steady_clock::now();

    // 转换为微秒
    long long elapsedMicroseconds = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    // 因为 scheduleNext() 第一次失败后立即退出，
    // 剩下的 Job 全部还在 waiting queue 中
    const std::size_t waitingJobs = scheduler.waitingJobCount();

    return {
        scheduledJobs,
        waitingJobs,
        elapsedMicroseconds
    };
}

std::string policyToString(SchedulingPolicy policy) {
    switch (policy) {
        case SchedulingPolicy::FirstFit:
            return "FirstFit";

        case SchedulingPolicy::BinPack:
            return "BinPack";

        case SchedulingPolicy::LeastAllocated:
            return "LeastAllocated";
    }

    return "Unknown";
}

int main() {

    constexpr std::size_t JOB_COUNT = 100;
    constexpr unsigned int SEED = 42;

    // 所有策略共用完全相同的一组 Job
    std::vector<Job> jobs = generateJobs(JOB_COUNT, SEED);

    std::vector<SchedulingPolicy> policies = {
        SchedulingPolicy::FirstFit,
        SchedulingPolicy::BinPack,
        SchedulingPolicy::LeastAllocated
    };

    std::cout
        << std::left
        << std::setw(18) << "Policy"
        << std::setw(15) << "Scheduled"
        << std::setw(15) << "Waiting"
        << std::setw(15) << "Time (us)"
        << '\n';

    std::cout << std::string(63, '-') << '\n';

    for (SchedulingPolicy policy : policies) {

        BenchmarkResult result = runBenchmark(policy, jobs);

        std::cout
            << std::left
            << std::setw(18) << policyToString(policy)
            << std::setw(15) << result.scheduleJobs
            << std::setw(15) << result.waitingJobs
            << std::setw(15) << result.elapsedMicroseconds
            << '\n';
    }

    return 0;
}