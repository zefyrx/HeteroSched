#include <iostream>
#include <vector>
#include <random>
#include <iomanip>
#include <chrono>
#include <optional>

#include "core/job.h"
#include "core/scheduler.h"

enum class StopReason {
    AllScheduled,               // 所有 job 都成功调度
    InsufficientCapacity,       // 集群总可用 CPU 和总可用内存不满足 Job
    ResourceFragmentation,      // 集群总可用 CPU 和总可用内存满足 Job
    UnexpectedFailure           // 有能调度的 worker 却未调度，发生了 UnexpectedFailure
};

struct BenchmarkResult {
    std::size_t scheduledJobs;
    std::size_t waitingJobs;
    double cpuUtilization;
    double memoryUtilization;
    long long elapsedNanoseconds;
    StopReason stopReason;
};

StopReason classifyStopReason(const std::vector<WorkerSnapshot>& snapshots, const Job* blockedJob) {
    if (blockedJob == nullptr) {
        return StopReason::AllScheduled;
    }

    long long availableCpu = 0;
    long long availableMemory = 0;

    for (const auto& worker : snapshots) {
        if (worker.availableResource.cpuCores >= blockedJob->requiredResource.cpuCores && worker.availableResource.memoryMB >= blockedJob->requiredResource.memoryMB) {
            return StopReason::UnexpectedFailure;
        }
        availableCpu += worker.availableResource.cpuCores;
        availableMemory += worker.availableResource.memoryMB;
    }

    if (availableCpu >= blockedJob->requiredResource.cpuCores && availableMemory >= blockedJob->requiredResource.memoryMB) {
        return StopReason::ResourceFragmentation;
    }

    return StopReason::InsufficientCapacity;
}

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

std::optional<BenchmarkResult> runBenchmark(SchedulingPolicy policy, const std::vector<Job>& jobs) {

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
            return std::nullopt;
        }
    }

    std::size_t scheduledJobs = 0;

    // 从这里才开始计时
    auto start = std::chrono::steady_clock::now();

    while (scheduler.scheduleNext()) {
        ++scheduledJobs;
    }

    auto end = std::chrono::steady_clock::now();

    // 转换为纳秒
    const long long elapsedNanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();

    // 因为 scheduleNext() 第一次失败后立即退出，
    // 剩下的 Job 全部还在 waiting queue 中
    const std::size_t waitingJobs = scheduler.waitingJobCount();

    const auto snapshots = scheduler.getWorkerSnapshots();

    const Job* blockedJob = scheduledJobs < jobs.size() ? &jobs[scheduledJobs] : nullptr;
    StopReason stopReason = classifyStopReason(snapshots, blockedJob);

    long long totalCpu = 0;
    long long availableCpu = 0;
    long long totalMemory = 0;
    long long availableMemory = 0;

    for (const auto& worker : snapshots) {
        totalCpu += worker.totalResource.cpuCores;
        availableCpu += worker.availableResource.cpuCores;

        totalMemory += worker.totalResource.memoryMB;
        availableMemory += worker.availableResource.memoryMB;
    }

    const double cpuUtilization = totalCpu == 0 ? 0.0 : 100.0 * (totalCpu - availableCpu) / totalCpu;
    const double memoryUtilization = totalMemory == 0 ? 0.0 : 100.0 * (totalMemory - availableMemory) / totalMemory;

    return BenchmarkResult {
        scheduledJobs,
        waitingJobs,
        cpuUtilization,
        memoryUtilization,
        elapsedNanoseconds,
        stopReason
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

std::string stopReasonToString(StopReason reason) {
    switch (reason) {
        case StopReason::AllScheduled:
            return "AllScheduled";

        case StopReason::InsufficientCapacity:
            return "InsufficientCapacity";

        case StopReason::ResourceFragmentation:
            return "ResourceFragmentation";

        case StopReason::UnexpectedFailure:
            return "UnexpectedFailure";
    }

    return "Unknown";
}

int main() {

    constexpr std::size_t JOB_COUNT = 100;
    constexpr std::size_t REPEAT_COUNT = 10000;
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
        << std::setw(15) << "CPU Util (%)"
        << std::setw(18) << "Memory Util (%)"
        << std::setw(20) << "Avg Schedule (ns)"
        << std::setw(24) << "StopReason"
        << '\n';

    std::cout << std::string(125, '-') << '\n';

    for (SchedulingPolicy policy : policies) {
        std::optional<BenchmarkResult> baseline;
        long long totalNanoseconds = 0;

        for (std::size_t run = 0; run < REPEAT_COUNT; ++run) {
            const auto result = runBenchmark(policy, jobs);

            if (!result) {
                return 1;
            }

            if (!baseline) {
                baseline = result;
            } else if (result->scheduledJobs != baseline->scheduledJobs ||
                       result->waitingJobs != baseline->waitingJobs ||
                       result->stopReason != baseline->stopReason) {
                std::cerr << "Benchmark produced inconsistent results\n";
                return 1;
            }

            totalNanoseconds += result->elapsedNanoseconds;
        }

        const std::size_t attemptsPerRun = baseline->scheduledJobs + 1;
        const double totalAttempts =
            static_cast<double>(REPEAT_COUNT) * attemptsPerRun;
        const double averageNanosecondsPerAttempt =
            static_cast<double>(totalNanoseconds) / totalAttempts;

        std::cout
            << std::left
            << std::setw(18) << policyToString(policy)
            << std::setw(15) << baseline->scheduledJobs
            << std::setw(15) << baseline->waitingJobs
            << std::setw(15) << std::fixed << std::setprecision(2) << baseline->cpuUtilization
            << std::setw(18) << std::fixed << std::setprecision(2) << baseline->memoryUtilization
            << std::setw(20) << std::fixed << std::setprecision(2) << averageNanosecondsPerAttempt
            << std::setw(24) << stopReasonToString(baseline->stopReason)
            << '\n';
    }

    return 0;
}
