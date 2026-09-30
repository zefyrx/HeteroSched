#ifndef HETEROSCHED_SCHEDULER_H
#define HETEROSCHED_SCHEDULER_H

#include <cstddef>
#include <optional>
#include <vector>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <mutex>

#include "job.h"
#include "worker.h"

enum class SchedulingPolicy {
    FirstFit,
    BinPack,
    LeastAllocated
};

struct ScheduleResult {
    int jobId;
    int workerId;
};

struct WorkerSnapshot {
    int workerId;
    Resource totalResource;
    Resource availableResource;
};

class Scheduler {
public:
    // 创建 Scheduler，并设置其使用的调度策略
    explicit Scheduler(
        SchedulingPolicy policy = SchedulingPolicy::FirstFit);

    // 将一个 Worker 注册到 Scheduler 管理的 Worker 列表中
    void addWorker(const Worker& worker);

    // 将 Job 加入 FIFO 等待队列；Scheduler 生命周期内 jobId 不可重复
    bool submitJob(const Job& job);

    // 尝试调度队首 Job，并在成功后将其移入运行集合
    std::optional<ScheduleResult> scheduleNext();

    // 返回 Scheduler 当前管理的 Worker 数量
    std::size_t workerCount() const;

    // 返回 FIFO 队列中等待调度的 Job 数量
    std::size_t waitingJobCount() const;

    // 完成指定运行中 Job，释放其资源并删除运行记录
    bool completeJob(int jobId);

    // 返回当前已经分配但尚未完成的 Job 数量
    std::size_t runningJobCount() const;

    // 返回同一时刻所有 Worker 的资源状态副本
    std::vector<WorkerSnapshot> getWorkerSnapshots() const;

private:
    struct RunningJob {
        Job job;
        std::size_t workerIndex;
    };

    mutable std::mutex mutex_;

    std::unordered_map<int, RunningJob> runningJobs_;       // <jobId, RunningJob>
    std::unordered_set<int> submittedJobIds_;

    std::vector<Worker> workers_;
    std::queue<Job> waitingJobs_;
    SchedulingPolicy policy_;

    // 为 Job 选择 Worker、分配资源并返回 Worker 下标
    std::optional<std::size_t> schedule(const Job& job);

    // 根据当前策略选择一个能够运行 Job 的 Worker 下标
    std::optional<std::size_t> selectWorker(const Job& job) const;

    // 返回按注册顺序找到的第一个可运行 Job 的 Worker 下标
    std::optional<std::size_t> findFirstFitWorker(const Job& job) const;

    // 返回放置 Job 后剩余资源最少的 Worker 下标
    std::optional<std::size_t> findBinPackWorker(const Job& job) const;

    // 返回放置 Job 后综合资源利用率最低的 Worker 下标
    std::optional<std::size_t> findLeastAllocatedWorker(const Job& job) const;

};

#endif
