#ifndef HETEROSCHED_WORKER_H
#define HETEROSCHED_WORKER_H

#include "core/job.h"
#include "core/resource.h"

class Worker {
public:
    // 创建 Worker，并将可用资源初始化为全部资源
    Worker(int workerId, const Resource& totalResource);

    // 返回 Worker 的唯一标识符
    int getId() const;

    // 判断当前可用资源是否能够满足指定 Job
    bool canRun(const Job& job) const;

    // 在资源充足时为 Job 扣除所需资源
    bool allocate(const Job& job);

    // 释放 Job 占用的 CPU 和内存资源
    bool release(const Job& job);

    // 返回 Worker 当前的可用资源
    const Resource& getAvailableResource() const;

    // 返回 Worker 创建时配置的总资源
    const Resource& getTotalResource() const;

private:
    int workerId_;
    Resource totalResource_;
    Resource availableResource_;
};

#endif
