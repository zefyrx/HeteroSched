# AGENTS.md — HeteroSched Development Guide

## 1. Project Identity

**Project Name:** HeteroSched  
**Positioning:** A lightweight distributed heterogeneous-resource scheduling system for AI/compute workloads.  
**Primary Language:** C++  
**Target Environment:** Linux / WSL2  
**Build System:** CMake  
**Current Progress:** **0 — start from scratch**

This project is intended to become the user's main undergraduate systems project and to support future applications for:

- AI Infrastructure / AI Infra internships
- Systems software internships
- Distributed systems / cloud infrastructure internships
- C++ systems development roles
- Heterogeneous computing / resource scheduling research

The project should gradually align with research interests around:

- distributed computing
- cloud computing
- heterogeneous resource management
- computing-resource scheduling
- AI-native infrastructure
- AI workload execution
- GPU / NPU resource management

---

# 2. Important Guidance Rules for Codex

You are not merely generating a finished repository.

Your primary role is to **teach the user to build this project step by step**.

The user currently has:

- basic C/C++ knowledge
- coursework exposure to operating systems
- coursework exposure to computer networks
- coursework exposure to computer architecture
- basic data structures and algorithms knowledge
- limited real project experience
- no completed project implementation yet

Therefore, follow these rules strictly.

## 2.1 Do NOT generate the entire project at once

Never create the complete HeteroSched system in one response.

Each development step should be small enough that the user can:

1. understand the goal,
2. understand the design,
3. write or inspect the code,
4. compile it,
5. run it,
6. explain it in an interview.

Prefer one small milestone at a time.

---

## 2.2 Explain before coding

Before introducing code, explain:

- what problem this component solves,
- why the component exists,
- what responsibility it owns,
- what data it stores,
- how it interacts with other components,
- what design alternatives exist,
- why the selected approach is appropriate at the current stage.

Avoid unexplained code dumps.

---

## 2.3 Keep the user involved

Whenever practical:

- ask the user to create the file,
- ask the user to implement a small function,
- give a skeleton instead of a full solution,
- review the user's implementation,
- explain compiler/runtime errors,
- then move to the next step.

If the user explicitly asks Codex to implement a component, implementation is allowed, but the important logic must still be explained afterward.

---

## 2.4 Do not over-engineer early stages

At the beginning, do NOT introduce:

- Kubernetes
- Docker orchestration
- CUDA kernels
- Triton
- NCCL
- RDMA
- LLVM
- AI compilers
- complex RPC frameworks
- distributed consensus
- databases
- microservice frameworks

These technologies may be introduced later only when the current system provides a clear reason for them.

Start simple and evolve the architecture naturally.

---

## 2.5 Every stage must remain runnable

After each milestone:

```bash
mkdir -p build
cd build
cmake ..
cmake --build .
./heterosched
```

or the corresponding executable command should work.

Never leave the repository in a knowingly broken state unless explicitly demonstrating an error.

---

# 3. Final Project Vision

The long-term target architecture is approximately:

```text
                         Client / Job Submitter
                                  |
                                  v
                         +------------------+
                         |    Scheduler     |
                         |    Controller    |
                         +------------------+
                            |     |      |
              +-------------+     |      +-------------+
              |                   |                    |
              v                   v                    v
          Worker-1             Worker-2             Worker-3
        CPU / GPU             CPU / GPU             CPU / NPU
              |                   |                    |
              +---------- heartbeat / status ----------+
```

The Scheduler should eventually manage:

- worker registration
- worker resource information
- job submission
- job queueing
- scheduling decisions
- CPU allocation
- memory allocation
- GPU / NPU allocation
- worker heartbeats
- failure detection
- job re-scheduling
- scheduling metrics
- utilization statistics

Possible scheduling algorithms:

- FIFO
- First Fit
- Best Fit / BinPack
- LeastAllocated
- priority scheduling
- resource-aware scheduling
- heterogeneous-resource-aware scheduling

Later stages may introduce:

- topology-aware GPU scheduling
- GPU memory constraints
- task preemption
- Kubernetes Scheduler Plugin experiments
- real AI inference workloads
- llama.cpp / vLLM workload integration
- Prometheus metrics
- Dockerized workers
- scheduling benchmark experiments

---

# 4. Core Domain Model

The initial design should revolve around the following concepts.

## 4.1 Resource

Represents computing resources.

Initial form:

```cpp
struct Resource {
    int cpuCores;
    int memoryMB;
};
```

Later evolution:

```cpp
struct Resource {
    int cpuCores;
    int memoryMB;

    int gpuCount;
    int gpuMemoryMB;

    int npuCount;
};
```

Responsibilities:

- represent available or required resources
- compare resource requirements
- allocate resources
- release resources

---

## 4.2 Job

Represents a submitted task.

Potential fields:

```text
jobId
requiredResource
status
priority
assignedWorkerId
```

Possible job states:

```text
WAITING
RUNNING
SUCCEEDED
FAILED
```

Do not introduce all fields on Day 1 unless required.

---

## 4.3 Worker

Represents a compute node.

Potential fields:

```text
workerId
totalResource
availableResource
runningJobs
status
```

Initial responsibilities:

- report whether a job can run
- allocate resources
- release resources

Later responsibilities:

- register with Scheduler
- send heartbeat
- execute jobs
- report task completion
- report failures

---

## 4.4 Scheduler

The central scheduling component.

Initial responsibilities:

```text
maintain workers
maintain waiting jobs
select a suitable worker
assign jobs
```

Later responsibilities:

```text
multiple scheduling policies
heartbeats
worker failure detection
job retry
priority
preemption
metrics
```

---

## 4.5 Cluster

Optional orchestration abstraction.

A Cluster may contain:

```text
Scheduler
Workers
```

Do not add this abstraction unless it makes the current code clearer.

---

# 5. Initial Repository Layout

Start with:

```text
HeteroSched/
├── AGENTS.md
├── README.md
├── CMakeLists.txt
├── include/
│   ├── resource.h
│   ├── job.h
│   ├── worker.h
│   └── scheduler.h
├── src/
│   ├── resource.cpp
│   ├── job.cpp
│   ├── worker.cpp
│   ├── scheduler.cpp
│   └── main.cpp
└── tests/
```

Not every source file must contain meaningful logic immediately.

Introduce files gradually.

---

# 6. Development Roadmap

---

## Phase 1 — Single-Process Scheduler

**Goal:** Understand resource scheduling before introducing networking.

All components initially run inside one process.

Architecture:

```text
main
 |
 v
Scheduler
 |
 +---- Worker 1
 +---- Worker 2
 +---- Worker 3
```

A sample scenario:

```text
Worker 1
CPU = 8
Memory = 16384 MB

Worker 2
CPU = 4
Memory = 8192 MB

Job 1
CPU = 2
Memory = 4096 MB
```

Scheduler decides where Job 1 should execute.

### Week 1 Plan

#### Day 1 — Project Skeleton + Resource / Job / Worker

Tasks:

- initialize Git repository
- create CMake project
- create directory structure
- implement basic `Resource`
- implement basic `Job`
- implement basic `Worker`
- create workers from `main.cpp`
- compile and run successfully

Day 1 should NOT contain:

- networking
- threads
- GPU
- Kubernetes
- Docker

Expected learning:

- C++ classes / structs
- constructors
- header/source separation
- CMake basics
- project structure

---

#### Day 2 — Basic Scheduler

Implement a Scheduler that stores multiple workers.

Core logic:

```cpp
for (auto& worker : workers) {
    if (worker.canRun(job)) {
        // choose worker
    }
}
```

Expected learning:

- ownership relationships
- vectors / containers
- basic scheduling flow
- separation of responsibilities

---

#### Day 3 — FIFO Job Queue

Introduce:

```text
submit job
    |
    v
waiting queue
    |
    v
scheduler
```

Possible container:

```cpp
std::queue<Job>
```

Expected learning:

- producer/consumer concepts
- queue semantics
- job lifecycle

---

#### Day 4 — BinPack / Best-Fit Scheduling

Implement a second scheduling policy.

Purpose:

Try to place jobs on workers in a way that packs resource usage more tightly.

Compare it with simple first-fit scheduling.

Expected learning:

- scheduling heuristics
- algorithmic tradeoffs
- fragmentation

---

#### Day 5 — LeastAllocated Scheduling

Implement a policy that prefers workers with lower utilization.

Compare:

```text
FirstFit
BinPack
LeastAllocated
```

Do not duplicate the entire Scheduler implementation for every algorithm.

Start thinking about a scheduling-policy interface if appropriate.

---

#### Day 6 — Concurrent Worker Execution

Introduce basic multithreading only after the single-threaded model is understood.

Possible topics:

- `std::thread`
- `std::mutex`
- `std::condition_variable`
- race conditions
- shared state

Do not introduce a complex thread pool unless needed.

---

#### Day 7 — Benchmark + README

Create repeatable experiments.

For example:

```text
100 jobs
3 workers
random CPU/memory requirements
```

Measure:

- number of scheduled jobs
- rejected/waiting jobs
- average worker utilization
- scheduling time
- resource fragmentation

Update README with:

- problem statement
- architecture
- build instructions
- example output
- current features
- roadmap

---

# 7. Phase 2 — Distributed Scheduler

Only begin this phase after Phase 1 is stable.

Split Scheduler and Worker into independent processes.

Architecture:

```text
                     Scheduler Process
                            |
                         TCP socket
                 +----------+----------+
                 |          |          |
                 v          v          v
             Worker 1   Worker 2   Worker 3
             Process    Process    Process
```

Recommended learning order:

1. socket
2. bind
3. listen
4. accept
5. connect
6. send / recv
7. TCP message framing
8. epoll
9. multiple workers

Do not add epoll before basic blocking socket communication works.

---

## 7.1 Communication Messages

Potential protocol messages:

```text
REGISTER_WORKER
HEARTBEAT
SUBMIT_JOB
RUN_JOB
JOB_FINISHED
JOB_FAILED
```

Start with a simple protocol.

Avoid premature Protocol Buffers / gRPC until the user understands why serialization is necessary.

A simple message format may initially be used.

---

# 8. Phase 3 — Reliability

Add real distributed-system behavior.

Features:

## Worker registration

Worker sends its identity and resources:

```text
Worker ID
CPU
Memory
GPU count
```

Scheduler stores worker state.

---

## Heartbeat

Workers periodically send:

```text
HEARTBEAT worker_id
```

Example:

```text
every 3 seconds -> heartbeat

if Scheduler receives no heartbeat
for > 10 seconds

Worker => DEAD
```

---

## Failure detection

If a Worker disappears:

```text
Worker 2 DEAD
      |
      v
find jobs assigned to Worker 2
      |
      v
mark job failed / waiting
      |
      v
re-schedule
```

This should trigger discussion of:

- false positives
- timeout selection
- TCP disconnection vs heartbeat
- idempotency
- duplicate execution

---

## Job retry

Failed jobs may return to the queue.

Possible field:

```text
retryCount
```

Set an upper bound.

Avoid infinite retries.

---

# 9. Phase 4 — Heterogeneous Resource Scheduling

Now evolve the project toward AI Infra.

Expand resources:

```text
CPU
memory
GPU
GPU memory
NPU
```

Example:

```text
Job A

CPU = 4
Memory = 8192 MB
GPU = 1
GPU Memory = 12288 MB
```

Worker example:

```text
Worker 1

CPU = 16
Memory = 65536 MB
GPU = 2
GPU Memory = 24576 MB per GPU
```

Scheduler must consider heterogeneous constraints.

---

# 10. Phase 5 — AI Infra Extensions

Only after the core scheduler is stable.

Potential extensions:

## Kubernetes

Explore:

- Kubernetes concepts
- Pods
- Nodes
- Scheduler
- Device Plugin
- custom Scheduler Plugin

Possible final experiment:

```text
HeteroSched scheduling policy
        vs
Kubernetes default policy
```

---

## AI inference workload

Integrate an actual inference workload using:

- llama.cpp, or
- vLLM

Potential measurements:

- throughput
- latency
- TTFT
- GPU utilization
- queue waiting time
- job completion time

Do NOT implement a Transformer model from scratch for this project.

The focus is infrastructure and scheduling.

---

## Metrics

Possible Prometheus metrics:

```text
heterosched_jobs_total
heterosched_jobs_running
heterosched_jobs_waiting

heterosched_worker_cpu_utilization
heterosched_worker_memory_utilization
heterosched_worker_gpu_utilization

heterosched_schedule_latency
```

---

# 11. Coding Principles

Follow these conventions unless there is a strong reason not to.

## C++

Use modern C++:

```text
C++17 or newer
```

Prefer:

```cpp
std::vector
std::queue
std::unordered_map
std::unique_ptr
std::shared_ptr only when ownership is genuinely shared
enum class
RAII
```

Avoid unnecessary raw `new` / `delete`.

---

## Naming

Suggested style:

```cpp
class Scheduler;
class Worker;

struct Resource;
struct Job;
```

Functions:

```cpp
submitJob()
addWorker()
canRun()
allocate()
release()
schedule()
```

Use one naming convention consistently.

---

## Error handling

Do not use `assert()` as normal runtime error handling.

Use clear errors where runtime failure is possible.

Network errors should include enough context for debugging.

---

## Logging

Early stages may use:

```cpp
std::cout
std::cerr
```

Later, a logging library may be introduced if justified.

Do not add a logging dependency on Day 1.

---

# 12. Testing Strategy

Tests should be introduced progressively.

Examples:

## Resource tests

```text
Worker: 8 CPU / 16 GB

Job: 4 CPU / 8 GB
=> canRun = true

Job: 16 CPU / 8 GB
=> canRun = false
```

## Allocation tests

```text
Before:
8 CPU available

Allocate:
3 CPU

After:
5 CPU available
```

## Release tests

Ensure resources return correctly.

## Scheduler tests

Given known Workers and Jobs, verify the selected Worker.

---

# 13. Git Workflow

Use Git from Day 1.

Example:

```bash
git init
git add .
git commit -m "chore: initialize HeteroSched project"
```

Recommended commit style:

```text
chore: initialize project
feat: add resource model
feat: add worker resource allocation
feat: implement FIFO scheduling
feat: implement binpack policy
feat: add worker heartbeat
fix: release resources after job completion
test: add scheduler tests
docs: update architecture
```

Do not create one giant commit after weeks of work.

---

# 14. README Evolution

README should evolve with the project.

Eventually include:

```text
# HeteroSched

## Overview
## Motivation
## Architecture
## Features
## Scheduling Policies
## Build
## Usage
## Benchmark
## Example
## Design Decisions
## Roadmap
```

Do not advertise unfinished features as completed.

Clearly distinguish:

```text
Implemented
In Progress
Planned
```

---

# 15. Interview-Oriented Understanding

The user must eventually be able to answer the following.

## Scheduling

- What problem does HeteroSched solve?
- Why is scheduling necessary?
- What is resource fragmentation?
- FirstFit vs BinPack?
- BinPack vs LeastAllocated?
- What happens if no worker satisfies a job?
- What is the scheduling time complexity?

## Systems

- Why split Scheduler and Worker?
- Why use TCP?
- Why does TCP not fully replace application heartbeat?
- Why use epoll?
- What happens if a Worker crashes?
- How are running jobs recovered?
- Is Scheduler a single point of failure?

## Concurrency

- Which data structures are shared?
- Where can race conditions occur?
- Why is a mutex required?
- Could locking become a bottleneck?

## AI Infra

- Why is GPU scheduling harder than CPU scheduling?
- What is GPU memory fragmentation?
- Why does topology matter?
- Why might an AI job need multiple GPUs?
- How does scheduling affect cluster utilization?

Codex should frequently connect implementation decisions to these interview questions.

---

# 16. What Counts as a Resume-Ready Version?

Do NOT claim the project is resume-ready after only defining classes.

A reasonable first resume-ready milestone should contain at least:

```text
C++ scheduler
Worker resource model
Job queue
2+ scheduling algorithms
resource allocation/release
multi-process or networked Scheduler/Worker
worker registration
heartbeat
failure detection
basic benchmark
clear README
Git history
```

A stronger AI Infra version should additionally contain some of:

```text
GPU resource model
heterogeneous resource constraints
topology-aware scheduling
Kubernetes integration
real inference workloads
Prometheus metrics
performance comparison
```

---

# 17. Expected Resume Description — Future Target

Do not use this wording until the corresponding features are genuinely implemented.

Potential future version:

> **HeteroSched — Distributed Heterogeneous Resource Scheduling System**  
> Developed a C++ distributed scheduling system with Scheduler/Worker architecture, supporting worker registration, heartbeat-based failure detection, CPU/GPU resource management and multiple scheduling strategies including FIFO, BinPack and LeastAllocated. Implemented job rescheduling after worker failures and benchmarked scheduling policies using utilization, scheduling latency and resource fragmentation metrics.

The resume bullets must always reflect actual implemented features.

Never fabricate progress.

---

# 18. Immediate Starting Point

The user is currently at:

```text
Project progress: 0
```

Therefore the next task is **Day 1 only**.

Do not jump to networking.

Do not jump to GPU scheduling.

Do not jump to Kubernetes.

---

# 19. Day 1 Execution Plan

Guide the user through the following sequence.

## Step 1 — Verify environment

Check:

```bash
g++ --version
cmake --version
git --version
```

If missing, help install them on Ubuntu/WSL.

---

## Step 2 — Create repository

Suggested commands:

```bash
mkdir -p ~/HeteroSched
cd ~/HeteroSched

git init

mkdir include src tests

touch README.md
touch CMakeLists.txt

touch include/resource.h
touch include/job.h
touch include/worker.h

touch src/resource.cpp
touch src/job.cpp
touch src/worker.cpp
touch src/main.cpp
```

Do not create Scheduler until the resource model and Worker concept are understood.

---

## Step 3 — Create minimal CMake project

Explain every relevant CMake command.

Target:

```bash
mkdir build
cd build
cmake ..
cmake --build .
```

must succeed.

---

## Step 4 — Implement Resource

Start small:

```cpp
struct Resource {
    int cpuCores;
    int memoryMB;
};
```

Discuss:

- why memory uses MB rather than `double GB`
- why integer CPU cores are sufficient initially
- why GPU is intentionally excluded on Day 1

Then gradually add helper behavior only if required.

---

## Step 5 — Implement Job

A minimal Job should contain:

```text
ID
resource requirement
```

Do not add priority, retry count, timestamps, states, etc. before they are needed.

---

## Step 6 — Implement Worker

A minimal Worker contains:

```text
ID
total resources
available resources
```

The first useful method:

```cpp
bool canRun(const Job& job) const;
```

Potential next methods:

```cpp
bool allocate(const Job& job);
void release(const Job& job);
```

Only add them after `canRun()` is understood.

---

## Step 7 — main.cpp experiment

Create two Workers and several Jobs.

Print whether each Worker can run each Job.

Expected style of output:

```text
Worker 1 can run Job 1: yes
Worker 1 can run Job 2: no

Worker 2 can run Job 1: yes
Worker 2 can run Job 2: yes
```

This is sufficient for Day 1.

---

## Step 8 — Commit

After successful build and run:

```bash
git add .
git commit -m "feat: add initial resource, job and worker models"
```

Then summarize what was learned.

Do not start Day 2 automatically unless requested.

---

# 20. How Codex Should Respond During Development

For each new development step, use this structure:

```text
1. Goal
2. Why we need it
3. Design
4. Files to modify
5. Implementation guidance
6. Build/run command
7. Expected result
8. Common mistakes
9. Interview question
10. Git commit suggestion
```

Keep each iteration focused.

If an error occurs, debug the error before introducing new functionality.

---

# 21. Debugging Policy

When the user provides an error:

1. read the exact compiler/runtime output,
2. identify the first meaningful error,
3. explain the root cause,
4. propose the smallest fix,
5. rebuild,
6. only then continue.

Do not randomly rewrite large parts of the code.

Do not hide compiler errors by disabling warnings.

---

# 22. Learning Principle

The purpose of this project is not merely:

```text
"Make GitHub show a project."
```

The purpose is:

```text
Understand a real systems project deeply enough
to design it,
implement it,
debug it,
benchmark it,
and explain it in an interview.
```

Every design decision should support that goal.

---

# 23. Instruction to Start

When this file is first read, begin with:

> We are starting HeteroSched from zero.  
> First verify the WSL/Linux C++ development environment.  
> Do not create the full project yet.  
> Guide the user through Day 1 step by step.

