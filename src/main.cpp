#include <iostream>

#include "job.h"
#include "worker.h"

void printCanRun(const Worker& worker, const Job& job) {
    std::cout << "Worker " << worker.getId()
              << " can run Job " << job.jobId << ": "
              << (worker.canRun(job) ? "yes" : "no")
              << '\n';
}

int main() {
    Worker worker1{1, {8, 16384}};
    Worker worker2{2, {4, 8192}};

    Job job1{1, {2, 4096}};
    Job job2{2, {6, 12288}};
    Job job3{3, {10, 2048}};

    printCanRun(worker1, job1);
    printCanRun(worker1, job2);
    printCanRun(worker1, job3);

    std::cout << '\n';

    printCanRun(worker2, job1);
    printCanRun(worker2, job2);
    printCanRun(worker2, job3);

    return 0;
}