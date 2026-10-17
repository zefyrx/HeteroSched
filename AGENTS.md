# AGENTS.md — HeteroSched Development Guide

## 1. Project Identity

**Project Name:** HeteroSched  
**Positioning:** A lightweight distributed heterogeneous-resource scheduling system for AI/compute workloads.  
**Primary Language:** C++  
**Target Environment:** Linux / WSL2  
**Build System:** CMake  
**Current Progress:** **The core scheduler, TCP Scheduler/Worker workflow, LT epoll server, correctness tests, and reproducible blocking/epoll comparisons are implemented. The epoll optimization stage is closed for the internship-resume version.**

**Current user priority:** Keep the project understandable and suitable for internship applications. Do not keep expanding epoll or add graceful shutdown, ET, a thread pool, or timer infrastructure merely to make the project appear more complete. Prioritize a runnable demonstration, honest resume evidence, and interview understanding. Further features must address a concrete user need; the full long-term roadmap is not a prerequisite for presenting this version.

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
- has implemented HeteroSched resource scheduling, multiple policies, mutex-protected shared state, CMake/CTest, a blocking TCP Scheduler/Worker workflow, protocol validation, and a repeatable scheduler benchmark
- is currently learning Linux non-blocking I/O and epoll
- has limited experience with production-scale systems and performance engineering

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

# 16. Resume-Ready Criteria and Honest Positioning

Do not use the same bar for an initial resume-ready project and a more advanced AI Infra project.

## 16.1 Current Resume-Ready Baseline

A credible initial version should include:

```text
C++ scheduler and CPU/memory resource model
2+ scheduling policies
resource allocation and release
Scheduler/Worker communication over TCP
worker registration and job lifecycle workflow
input validation and protocol framing
concurrency protection for shared scheduler state
repeatable CTest coverage
reproducible benchmark with documented workload
clear README and build/run instructions
```

Heartbeat, automatic failure recovery, GPU scheduling, Kubernetes integration, and production-grade scalability are valuable extensions, but they are NOT prerequisites for describing the current project honestly as a lightweight TCP-based resource-scheduling prototype.

## 16.2 Stronger Systems Version

Prioritize a small number of verifiable improvements:

```text
non-blocking TCP server using epoll
correct per-connection input/output buffering
concurrent-client integration tests
end-to-end throughput and latency benchmark
P50/P95 latency, error rate, and CPU usage measurements
before/after comparison against the blocking baseline
```

## 16.3 Advanced AI Infra Version

Only consider these after the systems optimization above is correct and measured:

```text
worker heartbeat and failure detection
bounded task retry and duplicate-execution analysis
GPU/GPU-memory resource model
heterogeneous-resource-aware scheduling
real inference workload integration
Kubernetes or Prometheus integration when justified
```

Never claim a feature as implemented until it exists, is tested, and its limitations are documented. Prefer precise positioning such as "lightweight TCP-based resource scheduler prototype" over claiming a production-grade distributed heterogeneous scheduler.

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

Phase 1, the basic TCP workflow, and the focused epoll optimization milestones are complete. Do not restart historical milestones or automatically expand the networking layer.

Current implemented baseline includes:

- CPU and memory resource model
- Job and Worker domain models
- FIFO queue and FirstFit, BinPack, and LeastAllocated policies
- resource reservation/allocation and release
- Scheduler/Worker TCP workflow for registration, job submission, claiming, simulated execution, and completion reporting
- newline-delimited text protocol with input validation and message-size limits
- handling for partial sends and EINTR
- mutex-protected shared scheduler state
- CMake/CTest coverage and repeatable scheduler-policy benchmark
- single-threaded non-blocking LT epoll with per-connection buffering, partial writes, backpressure, and connection cleanup
- optional idle timeout and client connection limit, both disabled by default
- concurrent-client correctness tests and archived blocking/epoll, backlog, and slow-client experiments

The current internship version is ready to demonstrate. Keep the epoll milestone closed; use the README's implemented features, recorded experiments, and limitations for resume and interview preparation. Heartbeat/recovery, GPU resources, and real workloads remain optional future work, not automatic next steps.

For a future requested server change:

1. inspect the current repository and README instead of assuming the file layout;
2. run the existing build and tests and record the baseline result;
3. preserve the current blocking implementation or record a reproducible baseline commit so that comparisons are meaningful;
4. implement the smallest requested change that can be built and tested independently.

Do not add GPU scheduling, Kubernetes, heartbeat/recovery, a thread pool, a new RPC framework, or unrelated features as part of the epoll milestone. Those are separate future decisions.

# 19. Historical Day 1 Execution Plan — Completed

This section is retained as a learning record. Do not restart Day 1 unless the user explicitly asks to review or repair its foundations.

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

When this file is first read, begin by checking the repository's actual state rather than blindly following historical milestones.

> HeteroSched's scheduler, TCP workflow, and measured LT epoll implementation are complete for the current internship version. Inspect the actual repository and follow the user's current request. Do not reopen epoll development or start unrelated AI Infra features just because they appear in the historical roadmap. Keep changes small, runnable, and explainable; use recorded evidence for any resume claims.

---

# 24. Focused Optimization Roadmap: epoll and Non-Blocking TCP

**Status: completed and closed for the current internship version.** Milestones A–E below are retained as the implementation and validation record. Known limitations are documented in README; they do not require adding more networking features before using this project on a resume.

The goal is to demonstrate systems-engineering depth through correctness, concurrency, and measurement—not to maximize the number of features.

## 24.1 Milestone A — Establish a Baseline First

Before modifying the server:

- build the current project and run all CTest tests;
- record the exact command, environment, and test result;
- create or preserve a reproducible blocking-server benchmark;
- measure the existing implementation with multiple clients where possible;
- keep the existing scheduler-policy benchmark separate from the network-service benchmark.

Do not invent or estimate performance numbers. If a metric is not measured, label it as unavailable.

## 24.2 Milestone B — Implement the Smallest Correct epoll Server

Use a staged implementation. Explain each stage before coding.

1. Set the listening socket to non-blocking mode.
2. Create an epoll instance and register the listening socket.
3. Accept connections when the listening socket is readable; set each accepted client socket to non-blocking mode.
4. Start with **level-triggered (LT) epoll**. Do not begin with edge-triggered (ET) mode unless LT is complete, tested, and there is a measured reason to add ET complexity.
5. Maintain per-connection state, including an input buffer, an output buffer, and the current output offset.
6. Read available bytes until `EAGAIN`/`EWOULDBLOCK`, while correctly handling `EINTR`, EOF, and other errors.
7. Parse newline-delimited messages from the connection's input buffer. Support a message split across multiple reads and multiple complete messages arriving in one read. Enforce the existing protocol's maximum message length.
8. Handle partial writes: retain unsent bytes and resume when the socket becomes writable. Register `EPOLLOUT` only while output remains pending to avoid a busy loop.
9. Handle disconnect and error events (`EPOLLRDHUP`, `EPOLLHUP`, `EPOLLERR` as applicable) and remove/close the connection safely.
10. Ensure no client-specific operation can block the event loop. Do not rely on blocking `recv()` or `SO_RCVTIMEO` for readiness handling in the epoll path.

Keep the event loop single-threaded initially unless the existing architecture requires otherwise. Do not add a thread pool just to make the server appear more advanced. Reuse the existing Scheduler API and its synchronization model; do not bypass its invariants.

## 24.3 Milestone C — Correctness Tests

Add or extend tests for at least:

- a normal registration/request/response path;
- multiple clients connected at the same time;
- a message fragmented across multiple writes;
- multiple newline-delimited messages received together;
- malformed messages and invalid fields;
- a message exceeding the protocol length limit;
- a client disconnecting before completing a message;
- a client disconnecting while output is pending, where practical;
- one client that sends incomplete data must not prevent another client from making progress;
- no duplicate job delivery and no duplicate resource accounting after the network refactor.

Tests should verify externally observable behavior, not depend on arbitrary sleeps where a deterministic synchronization method is available. Existing tests must continue to pass.

## 24.4 Milestone D — Reproducible End-to-End Benchmark

Build a separate benchmark for the TCP service. Keep it distinct from the microbenchmark of the scheduling function.

Vary a small, documented set of client concurrency levels, for example 1, 10, 50, and 100 clients if the machine can support them. Use the same request mix and total workload for the blocking and epoll versions.

Record at least:

- completed requests and failed requests;
- throughput (requests/second);
- end-to-end latency, including P50 and P95;
- CPU usage and peak memory usage when practical;
- client concurrency, total request count, message sizes, machine/OS details, compiler/build mode, and test command.

Use a warm-up phase and repeat each scenario multiple times. Report the median across runs and include enough detail for another person to reproduce the result. Do not claim epoll is faster in every workload: at low concurrency, results may be similar or the new implementation may be slower. Explain the observed trade-offs based on measurements.

Do not mix scheduling-algorithm time with network latency without clearly defining the measurement boundary.

## 24.5 Milestone E — Documentation and Resume Evidence

Update the README only after the implementation and measurements are verified. Include:

- the blocking-server limitation that motivated the change;
- the epoll architecture and per-connection state model;
- the correctness cases covered by tests;
- benchmark environment and exact reproduction commands;
- a before/after results table with actual measurements;
- limitations and cases not yet handled.

A resume bullet may mention epoll, concurrent clients, or performance improvements only after those features are implemented and tested. Any percentage improvement must be computed from recorded measurements, not guessed.

## 24.6 Scope Guardrails

For this optimization cycle, prioritize:

1. correctness;
2. tests;
3. reproducible measurement;
4. clear explanation and documentation.

Do not combine this milestone with heartbeat/failure recovery, persistence, GPU scheduling, Kubernetes, TLS, Protocol Buffers/gRPC, or a new threading architecture. Do not turn HeteroSched into a large but unfinished platform. The CUDA GEMM project is a separate project and should not be mixed into this repository.

---

# 25. Codex Rules for Performance Work

When doing performance-related work:

- State the hypothesis before changing code.
- Define the metric and measurement boundary before benchmarking.
- Change one major variable at a time.
- Keep a reproducible baseline.
- Run correctness tests before trusting performance results.
- Avoid comparing debug and release builds or different workloads.
- Include failures and timeouts in the report; do not silently drop them.
- Explain complexity, bottlenecks, trade-offs, and what the measurements do and do not prove.
- If results do not improve, analyze why instead of fabricating a win or hiding the result.
- End each milestone with: what changed, tests run, measured results, known limitations, interview questions, and a suggested Git commit message.
