#include "worker.h"

Worker::Worker(int workerId, const Resource& totalResource) 
    : workerId_(workerId),
      totalResource_(totalResource),
      availableResource_(totalResource) {
}

int Worker::getId() const {
    return workerId_;
}

bool Worker::canRun(const Job& job) const {
    return availableResource_.cpuCores >= job.requiredResource.cpuCores && availableResource_.memoryMB >= job.requiredResource.memoryMB;
}

bool Worker::allocate(const Job& job) {
    if (!canRun(job)) {
        return false;
    }

    availableResource_.cpuCores -= job.requiredResource.cpuCores;
    availableResource_.memoryMB -= job.requiredResource.memoryMB;

    return true;
}

const Resource& Worker::getAvailableResource() const {
    return availableResource_;
}

bool Worker::release(const Job& job) {
    const int allocatedCpuCores = totalResource_.cpuCores - availableResource_.cpuCores;
    const int allocatedMemoryMB = totalResource_.memoryMB - availableResource_.memoryMB;

    if (allocatedCpuCores < job.requiredResource.cpuCores || allocatedMemoryMB < job.requiredResource.memoryMB) {
        return false;
    }

    availableResource_.cpuCores += job.requiredResource.cpuCores;
    availableResource_.memoryMB += job.requiredResource.memoryMB;

    return true;
}
