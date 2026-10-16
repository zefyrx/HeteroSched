#include <iostream>
#include <thread>

#include "core/scheduler.h"

namespace {

bool expect(bool condition, const char* description) {
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
    }
    return condition;
}

bool assigned(const FetchResult& result, int jobId, int cpu, int memory) {
    return result.status == FetchStatus::Assigned && result.job &&
           result.job->jobId == jobId &&
           result.job->requiredResource.cpuCores == cpu &&
           result.job->requiredResource.memoryMB == memory;
}

bool noJob(const FetchResult& result) {
    return result.status == FetchStatus::NoJob && !result.job;
}

bool available(const Scheduler& scheduler, int cpu, int memory) {
    const auto workers = scheduler.getWorkerSnapshots();
    return workers.size() == 1 &&
           workers[0].availableResource.cpuCores == cpu &&
           workers[0].availableResource.memoryMB == memory;
}

bool testUnknownWorkerDoesNotSchedule() {
    Scheduler scheduler;
    if (!scheduler.addWorker(Worker{0, {2, 4096}}) ||
        !scheduler.submitJob(Job{0, {2, 4096}})) {
        return false;
    }
    for (int workerId : {-1, 1}) {
        const auto result = scheduler.fetchJob(workerId);
        if (!expect(result.status == FetchStatus::UnknownWorker && !result.job &&
                    scheduler.waitingJobCount() == 1 &&
                    scheduler.runningJobCount() == 0 && available(scheduler, 2, 4096),
                    "unknown Worker must not trigger global scheduling")) {
            return false;
        }
    }
    return expect(assigned(scheduler.fetchJob(0), 0, 2, 4096),
                  "Worker and Job ID zero can be fetched");
}

bool testFetchCompletionLifecycle() {
    Scheduler scheduler;
    if (!scheduler.addWorker(Worker{1, {2, 4096}}) ||
        !expect(noJob(scheduler.fetchJob(1)), "empty queue returns NoJob") ||
        !scheduler.submitJob(Job{101, {2, 4096}}) ||
        !scheduler.submitJob(Job{102, {1, 1024}})) {
        return false;
    }

    if (!expect(assigned(scheduler.fetchJob(1), 101, 2, 4096) &&
                scheduler.waitingJobCount() == 1 && scheduler.runningJobCount() == 1 &&
                available(scheduler, 0, 0), "fetch schedules and reserves resources once") ||
        !expect(noJob(scheduler.fetchJob(1)) && available(scheduler, 0, 0),
                "fetched assignment cannot be fetched twice") ||
        !expect(scheduler.completeJob(101) && !scheduler.completeJob(101) &&
                available(scheduler, 2, 4096), "completion releases resources once") ||
        !expect(assigned(scheduler.fetchJob(1), 102, 1, 1024),
                "next poll schedules queued Job after completion") ||
        !scheduler.completeJob(102)) {
        return false;
    }
    return expect(noJob(scheduler.fetchJob(1)) && scheduler.waitingJobCount() == 0 &&
                  scheduler.runningJobCount() == 0 && available(scheduler, 2, 4096) &&
                  !scheduler.submitJob(Job{101, {1, 1024}}),
                  "drained Scheduler restores resources and retains Job ID uniqueness");
}

bool testGlobalSchedulingAndPendingPriority() {
    Scheduler scheduler;
    if (!scheduler.addWorker(Worker{1, {4, 8192}}) ||
        !scheduler.addWorker(Worker{2, {4, 8192}}) ||
        !scheduler.submitJob(Job{101, {1, 1024}}) ||
        !scheduler.submitJob(Job{102, {1, 1024}})) {
        return false;
    }
    if (!expect(noJob(scheduler.fetchJob(2)) && scheduler.waitingJobCount() == 1 &&
                scheduler.runningJobCount() == 1,
                "one fetch schedules only the head, possibly onto another Worker") ||
        !expect(assigned(scheduler.fetchJob(1), 101, 1, 1024) &&
                scheduler.waitingJobCount() == 1 && scheduler.runningJobCount() == 1,
                "own pending assignment is taken before scheduling another Job") ||
        !expect(noJob(scheduler.fetchJob(2)), "requesting Worker cannot steal assignment") ||
        !expect(assigned(scheduler.fetchJob(1), 102, 1, 1024),
                "target Worker receives assignment made by another poll")) {
        return false;
    }
    return scheduler.completeJob(101) && scheduler.completeJob(102) &&
           expect(noJob(scheduler.fetchJob(1)) && noJob(scheduler.fetchJob(2)),
                  "both Workers have no pending assignments after completion");
}

bool testManualCompletionAndPendingFifo() {
    Scheduler scheduler;
    if (!scheduler.addWorker(Worker{1, {4, 4096}})) {
        return false;
    }
    // Job ID order deliberately differs from sorting and unordered_map iteration.
    for (int jobId : {30, 10, 20}) {
        if (!scheduler.submitJob(Job{jobId, {1, 1024}}) || !scheduler.scheduleNext()) {
            return false;
        }
    }
    if (!scheduler.completeJob(10) ||
        !expect(assigned(scheduler.fetchJob(1), 30, 1, 1024) &&
                available(scheduler, 2, 2048), "fetch does not allocate a second time") ||
        !expect(assigned(scheduler.fetchJob(1), 20, 1, 1024),
                "manual completion removes middle pending entry and preserves FIFO") ||
        !expect(noJob(scheduler.fetchJob(1)), "delivered Jobs leave pending queue") ||
        !scheduler.completeJob(30) || !scheduler.completeJob(20)) {
        return false;
    }
    if (!scheduler.submitJob(Job{40, {1, 1024}}) || !scheduler.scheduleNext() ||
        !scheduler.completeJob(40)) {
        return false;
    }
    return expect(noJob(scheduler.fetchJob(1)) && scheduler.runningJobCount() == 0 &&
                  available(scheduler, 4, 4096),
                  "manually completing last undelivered Job leaves no stale dispatch");
}

bool testFetchPreservesStrictFifo() {
    Scheduler scheduler;
    if (!scheduler.addWorker(Worker{1, {2, 4096}}) ||
        !scheduler.submitJob(Job{101, {3, 1024}}) ||
        !scheduler.submitJob(Job{102, {1, 1024}})) {
        return false;
    }
    return expect(noJob(scheduler.fetchJob(1)) && scheduler.waitingJobCount() == 2 &&
                  scheduler.runningJobCount() == 0 && available(scheduler, 2, 4096),
                  "poll must not skip an unschedulable FIFO head");
}

bool testFetchUsesConfiguredPolicy() {
    for (const auto policy : {SchedulingPolicy::FirstFit, SchedulingPolicy::BinPack,
                              SchedulingPolicy::LeastAllocated}) {
        Scheduler scheduler{policy};
        if (!scheduler.addWorker(Worker{1, {4, 8192}}) ||
            !scheduler.addWorker(Worker{2, {8, 16384}}) ||
            !scheduler.submitJob(Job{101, {2, 4096}})) {
            return false;
        }
        const auto first = scheduler.fetchJob(1);
        const auto second = scheduler.fetchJob(2);
        const bool preferSecond = policy == SchedulingPolicy::LeastAllocated;
        if (!expect(preferSecond ? (noJob(first) && assigned(second, 101, 2, 4096))
                                 : (assigned(first, 101, 2, 4096) && noJob(second)),
                    "fetch must retain global policy's Worker selection")) {
            return false;
        }
    }
    return true;
}

bool testConcurrentFetchDoesNotDuplicateDelivery() {
    Scheduler scheduler;
    if (!scheduler.addWorker(Worker{1, {2, 4096}}) ||
        !scheduler.submitJob(Job{101, {2, 4096}})) {
        return false;
    }
    FetchResult first{FetchStatus::NoJob, std::nullopt};
    FetchResult second{FetchStatus::NoJob, std::nullopt};
    std::thread a([&] { first = scheduler.fetchJob(1); });
    std::thread b([&] { second = scheduler.fetchJob(1); });
    a.join();
    b.join();
    return expect(((assigned(first, 101, 2, 4096) && noJob(second)) ||
                   (noJob(first) && assigned(second, 101, 2, 4096))) &&
                  scheduler.runningJobCount() == 1 && scheduler.completeJob(101) &&
                  available(scheduler, 2, 4096),
                  "concurrent fetch delivers one assignment only once");
}

}  // namespace

int main() {
    if (!testUnknownWorkerDoesNotSchedule() || !testFetchCompletionLifecycle() ||
        !testGlobalSchedulingAndPendingPriority() || !testManualCompletionAndPendingFifo() ||
        !testFetchPreservesStrictFifo() || !testFetchUsesConfiguredPolicy() ||
        !testConcurrentFetchDoesNotDuplicateDelivery()) {
        return 1;
    }
    std::cout << "PASS: all job dispatch tests passed\n";
    return 0;
}
