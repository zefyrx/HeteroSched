#include <iostream>
#include <limits>
#include <thread>
#include <vector>

#include "core/job.h"
#include "core/scheduler.h"
#include "core/worker.h"

bool testBinPackChoosesTighterWorker() {
    Scheduler scheduler{SchedulingPolicy::BinPack};

    scheduler.addWorker(Worker{1, {8, 16384}});
    scheduler.addWorker(Worker{2, {4, 8192}});

    const Job job{1, {2, 4096}};

    scheduler.submitJob(job);
    const auto result = scheduler.scheduleNext();

    if (!result) {
        std::cerr << "FAIL: BinPack did not schedule the job\n";
        return false;
    }

    if (result->jobId != job.jobId) {
        std::cerr << "FAIL: scheduled the wrong job\n";
        return false;
    }

    if (result->workerId != 2) {
        std::cerr << "FAIL: BinPack selected the wrong worker\n";
        return false;
    }

    return true;
}

bool testFirstFitChoosesFirstSuitableWorker() {
    Scheduler scheduler{SchedulingPolicy::FirstFit};

    scheduler.addWorker(Worker{1, {4, 8192}});
    scheduler.addWorker(Worker{2, {8, 16384}});

    const Job job{1, {2, 4096}};

    scheduler.submitJob(job);
    const auto result = scheduler.scheduleNext();

    if (!result) {
        std::cerr << "FAIL: FirstFit did not schedule the job\n";
        return false;
    }

    if (result->jobId != job.jobId) {
        std::cerr << "FAIL: scheduled the wrong job\n";
        return false;
    }

    if (result->workerId != 1) {
        std::cerr << "FAIL: FirstFit selected the wrong worker\n";
        return false;
    }

    return true;
}

bool testLeastAllocatedChoosesLowerUtilizationWorker() {
    Scheduler scheduler{SchedulingPolicy::LeastAllocated};

    scheduler.addWorker(Worker{1, {4, 8192}});
    scheduler.addWorker(Worker{2, {8, 16384}});

    const Job job{1, {2, 4096}};

    scheduler.submitJob(job);
    const auto result = scheduler.scheduleNext();

    if (!result) {
        std::cerr << "FAIL: LeastAllocated did not schedule the job\n";
        return false;
    }

    if (result->jobId != job.jobId) {
        std::cerr << "FAIL: scheduled the wrong job\n";
        return false;
    }

    if (result->workerId != 2) {
        std::cerr << "FAIL: LeastAllocated selected the wrong worker\n";
        return false;
    }

    return true;
}

bool testUnschedulableJobRemainsQueued() {
    Scheduler scheduler{SchedulingPolicy::FirstFit};

    scheduler.addWorker(Worker{1, {2, 2048}});

    Job job{1, {4, 4096}};

    scheduler.submitJob(job);
    const auto result1 = scheduler.scheduleNext();

    if (result1) {
        std::cerr << "FAIL: Scheduler should not schedule the job\n";
        return false;
    }

    // 第一次调度失败后
    if (scheduler.waitingJobCount() != 1 || scheduler.runningJobCount() != 0) {
        std::cerr << "FAIL: unschedulable job was not kept waiting\n";
        return false;
    }

    scheduler.addWorker(Worker{2, {4, 4096}});
    const auto result2 = scheduler.scheduleNext();

    if (!result2) {
        std::cerr << "FAIL: Scheduler did not schedule the job\n";
        return false;
    }

    if (result2->jobId != job.jobId) {
        std::cerr << "FAIL: scheduled the wrong job\n";
        return false;
    }

    if (result2->workerId != 2) {
        std::cerr << "FAIL: Scheduler selected the wrong worker\n";
        return false;
    }

    // 第二次调度成功后
    if (scheduler.waitingJobCount() != 0 ||
        scheduler.runningJobCount() != 1) {
        std::cerr << "FAIL: queue counts are incorrect after scheduling\n";
        return false;
    }

    return true;
}

bool testRejectsDuplicateJobId() {
    Scheduler scheduler{SchedulingPolicy::FirstFit};

    scheduler.addWorker(Worker{1, {4, 8192}});
    scheduler.addWorker(Worker{2, {8, 16384}});

    const Job job{1, {2, 4096}};

    if (!scheduler.submitJob(job)) {
        return false;
    }
    
    // 尝试再次提交该 job
    if (scheduler.submitJob(job)) {
        return false;
    }

    if (scheduler.waitingJobCount() != 1) {
        return false;
    }

    const auto result = scheduler.scheduleNext();

    if (!result) {
        return false;
    }

    if (!scheduler.completeJob(result->jobId)) {
        return false;
    }

    if (scheduler.waitingJobCount() != 0) {
        return false;
    }

    if (scheduler.submitJob(job)) {
        return false;
    }

    if (scheduler.waitingJobCount() != 0) {
        return false;
    }

    return true;
}

bool testConcurrentJobCompletion() {
    Scheduler scheduler{SchedulingPolicy::FirstFit};

    scheduler.addWorker(Worker{1, {2, 4096}});
    scheduler.addWorker(Worker{2, {2, 4096}});

    const Job job1{1, {2, 4096}};
    const Job job2{2, {2, 4096}};

    if (!scheduler.submitJob(job1) || !scheduler.submitJob(job2)) {
        std::cerr << "FAIL: could not submit jobs for concurrent completion\n";
        return false;
    }

    const auto result1 = scheduler.scheduleNext();
    const auto result2 = scheduler.scheduleNext();

    if (!result1 || !result2) {
        std::cerr << "FAIL: could not schedule jobs for concurrent completion\n";
        return false;
    }

    if (scheduler.runningJobCount() != 2) {
        std::cerr << "FAIL: expected two running jobs before completion\n";
        return false;
    }

    bool completion1 = false;
    bool completion2 = false;

    std::thread completionThread1([&scheduler, &completion1, result1]() {
        completion1 = scheduler.completeJob(result1->jobId);
    });

    std::thread completionThread2([&scheduler, &completion2, result2]() {
        completion2 = scheduler.completeJob(result2->jobId);
    });

    completionThread1.join();
    completionThread2.join();

    if (!completion1 || !completion2) {
        std::cerr << "FAIL: concurrent job completion failed\n";
        return false;
    }

    if (scheduler.runningJobCount() != 0) {
        std::cerr << "FAIL: completed jobs remain in the running set\n";
        return false;
    }

    return true;
}

bool testWorkerSnapshotsReflectResourceChanges() {
    Scheduler scheduler{SchedulingPolicy::FirstFit};
    scheduler.addWorker(Worker{1, {4, 8192}});

    const Job job{1, {2, 2048}};

    if (!scheduler.submitJob(job) || !scheduler.scheduleNext()) {
        std::cerr << "FAIL: could not schedule snapshot test job\n";
        return false;
    }

    const auto allocatedSnapshots = scheduler.getWorkerSnapshots();

    if (allocatedSnapshots.size() != 1) {
        std::cerr << "FAIL: incorrect worker snapshot count\n";
        return false;
    }

    const WorkerSnapshot& allocated = allocatedSnapshots[0];

    if (allocated.workerId != 1 ||
        allocated.totalResource.cpuCores != 4 ||
        allocated.totalResource.memoryMB != 8192 ||
        allocated.availableResource.cpuCores != 2 ||
        allocated.availableResource.memoryMB != 6144) {
        std::cerr << "FAIL: snapshot does not reflect allocation\n";
        return false;
    }

    if (!scheduler.completeJob(job.jobId)) {
        std::cerr << "FAIL: could not complete snapshot test job\n";
        return false;
    }

    const auto releasedSnapshots = scheduler.getWorkerSnapshots();
    const WorkerSnapshot& released = releasedSnapshots[0];

    if (released.availableResource.cpuCores != 4 ||
        released.availableResource.memoryMB != 8192) {
        std::cerr << "FAIL: snapshot does not reflect resource release\n";
        return false;
    }

    return true;
}

bool testRejectsInvalidJobResources() {
    Scheduler scheduler{SchedulingPolicy::FirstFit};
    Worker worker{1, {4, 8192}};

    if (!scheduler.addWorker(worker)) {
        std::cerr << "FAIL: could not add worker for invalid job test\n";
        return false;
    }

    const std::vector<Job> invalidResourceJobs = {
        {10, {-1, 1024}},
        {11, {0, 1024}},
        {12, {1, -1}},
        {13, {1, 0}}
    };

    for (const Job& job : invalidResourceJobs) {
        if (scheduler.submitJob(job)) {
            std::cerr << "FAIL: Scheduler accepted Job " << job.jobId
                      << " with invalid resources\n";
            return false;
        }

        if (worker.canRun(job)) {
            std::cerr << "FAIL: Worker accepted Job " << job.jobId
                      << " with invalid resources\n";
            return false;
        }
    }

    const Job invalidIdJob{-1, {1, 1024}};

    if (scheduler.submitJob(invalidIdJob)) {
        std::cerr << "FAIL: Scheduler accepted a negative Job ID\n";
        return false;
    }

    if (scheduler.waitingJobCount() != 0) {
        std::cerr << "FAIL: invalid jobs entered the waiting queue\n";
        return false;
    }

    if (!scheduler.submitJob(Job{10, {1, 1024}})) {
        std::cerr << "FAIL: invalid Job attempt reserved its Job ID\n";
        return false;
    }

    return true;
}

bool testRejectsDuplicateWorkerId() {
    Scheduler scheduler{SchedulingPolicy::FirstFit};

    if (!scheduler.addWorker(Worker{1, {4, 8192}})) {
        std::cerr << "FAIL: Scheduler rejected the first Worker ID\n";
        return false;
    }

    if (scheduler.addWorker(Worker{1, {8, 16384}})) {
        std::cerr << "FAIL: Scheduler accepted a duplicate Worker ID\n";
        return false;
    }

    const auto snapshots = scheduler.getWorkerSnapshots();

    if (scheduler.workerCount() != 1 || snapshots.size() != 1) {
        std::cerr << "FAIL: duplicate Worker changed the worker count\n";
        return false;
    }

    if (snapshots[0].totalResource.cpuCores != 4 ||
        snapshots[0].totalResource.memoryMB != 8192) {
        std::cerr << "FAIL: duplicate Worker replaced the original Worker\n";
        return false;
    }

    return true;
}

bool testRejectsInvalidWorkerResources() {
    Scheduler scheduler{SchedulingPolicy::FirstFit};

    const std::vector<Worker> invalidWorkers = {
        Worker{-1, {4, 8192}},
        Worker{1, {0, 8192}},
        Worker{2, {-1, 8192}},
        Worker{3, {4, 0}},
        Worker{4, {4, -1}}
    };

    for (const Worker& worker : invalidWorkers) {
        if (scheduler.addWorker(worker)) {
            std::cerr << "FAIL: Scheduler accepted invalid Worker "
                      << worker.getId() << '\n';
            return false;
        }
    }

    if (scheduler.workerCount() != 0) {
        std::cerr << "FAIL: invalid Workers changed the worker count\n";
        return false;
    }

    if (!scheduler.addWorker(Worker{1, {1, 1024}})) {
        std::cerr << "FAIL: invalid Worker attempt reserved its Worker ID\n";
        return false;
    }

    if (!scheduler.addWorker(Worker{0, {1, 1024}})) {
        std::cerr << "FAIL: Scheduler rejected valid Worker ID 0\n";
        return false;
    }

    return true;
}

bool testWorkerAndJobIdsUseSeparateNamespaces() {
    Scheduler scheduler{SchedulingPolicy::FirstFit};

    if (!scheduler.addWorker(Worker{7, {2, 4096}})) {
        std::cerr << "FAIL: could not add Worker 7\n";
        return false;
    }

    const Job job{7, {1, 1024}};

    if (!scheduler.submitJob(job)) {
        std::cerr << "FAIL: Job ID collided with an equal Worker ID\n";
        return false;
    }

    const auto result = scheduler.scheduleNext();

    if (!result || result->jobId != 7 || result->workerId != 7) {
        std::cerr << "FAIL: equal Worker and Job IDs were not scheduled correctly\n";
        return false;
    }

    return true;
}

bool testWorkerRejectsInvalidRelease() {
    Worker worker{1, {4, 4096}};
    if (!worker.allocate(Job{1, {2, 2048}})) return false;
    for (const Resource resource : {Resource{-1, 1}, Resource{1, -1},
                                    Resource{0, 1}, Resource{1, 0},
                                    Resource{3, 1}, Resource{1, 2049}}) {
        if (worker.release(Job{2, resource}) ||
            worker.getAvailableResource().cpuCores != 2 ||
            worker.getAvailableResource().memoryMB != 2048) {
            std::cerr << "FAIL: invalid release changed available resources\n";
            return false;
        }
    }
    return worker.release(Job{1, {2, 2048}}) &&
           !worker.release(Job{1, {2, 2048}}) &&
           worker.getAvailableResource().cpuCores == 4 &&
           worker.getAvailableResource().memoryMB == 4096;
}

bool testMaximumResourcesAcrossPolicies() {
    const int maximum = std::numeric_limits<int>::max();
    for (auto policy : {SchedulingPolicy::FirstFit, SchedulingPolicy::BinPack,
                        SchedulingPolicy::LeastAllocated}) {
        Scheduler scheduler{policy};
        if (!scheduler.addWorker(Worker{maximum, {maximum, maximum}}) ||
            !scheduler.submitJob(Job{maximum, {maximum, maximum}}) ||
            !scheduler.submitJob(Job{0, {1, 1}})) return false;
        const auto result = scheduler.scheduleNext();
        if (!result || result->jobId != maximum || result->workerId != maximum ||
            scheduler.scheduleNext() || scheduler.runningJobCount() != 1 ||
            scheduler.waitingJobCount() != 1) return false;
        const auto reserved = scheduler.getWorkerSnapshots().front().availableResource;
        if (reserved.cpuCores != 0 || reserved.memoryMB != 0 ||
            !scheduler.completeJob(maximum) || scheduler.completeJob(maximum)) return false;
        const auto released = scheduler.getWorkerSnapshots().front().availableResource;
        if (released.cpuCores != maximum || released.memoryMB != maximum ||
            !scheduler.scheduleNext() || !scheduler.completeJob(0)) return false;
    }
    return true;
}

int main() {
    if (!testWorkerRejectsInvalidRelease() || !testMaximumResourcesAcrossPolicies()) return 1;
    if (!testBinPackChoosesTighterWorker()) {
        return 1;
    }

    if (!testFirstFitChoosesFirstSuitableWorker()) {
        return 1;
    }

    if (!testLeastAllocatedChoosesLowerUtilizationWorker()) {
        return 1;
    }

    if (!testUnschedulableJobRemainsQueued()) {
        return 1;
    }

    if (!testRejectsDuplicateJobId()) {
        return 1;
    }

    if (!testConcurrentJobCompletion()) {
        return 1;
    }

    if (!testWorkerSnapshotsReflectResourceChanges()) {
        return 1;
    }

    if (!testRejectsInvalidJobResources()) {
        return 1;
    }

    if (!testRejectsDuplicateWorkerId()) {
        return 1;
    }

    if (!testRejectsInvalidWorkerResources()) {
        return 1;
    }

    if (!testWorkerAndJobIdsUseSeparateNamespaces()) {
        return 1;
    }

    std::cout << "PASS: all scheduler tests passed\n";
    return 0;
}
