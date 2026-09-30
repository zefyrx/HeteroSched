# HeteroSched

HeteroSched 是一个使用 C++17 逐步构建的轻量级资源调度器学习项目。当前版本完成了单进程调度器：它管理 Worker、FIFO Job 队列、CPU/内存资源和多种放置策略，并通过线程模拟多个 Job 并发执行。

项目当前仍处于基础阶段，重点是理解资源分配、调度策略、线程安全和可重复实验，不包含网络通信、GPU 调度或真实任务执行。

## 当前架构

```text
                         main
                           |
                           v
                 +-------------------+
                 | Scheduler (mutex) |
                 +-------------------+
                   |       |       |
                   v       v       v
                Worker  Worker  Worker
                   ^               ^
                   | completeJob() |
                   +--- execution threads
```

Scheduler 拥有 Worker 状态、FIFO 等待队列和运行中 Job 记录。Worker 保存总资源与可用资源，并负责资源可行性检查、分配和释放。示例程序使用 `std::thread` 模拟 Job 执行；执行线程完成后通过线程安全的 `completeJob()` 释放资源。

## 已实现功能

- C++17 和 CMake 构建
- CPU 核数与内存资源模型
- 最小 Job 模型：Job ID 和资源需求
- Worker 总资源与可用资源管理
- Worker 资源检查、分配和释放
- FIFO Job 等待队列
- 运行中 Job 记录和完成处理
- Scheduler 生命周期内拒绝重复 `jobId`
- FirstFit、BinPack 和 LeastAllocated 调度策略
- 使用 mutex 保护 Scheduler 共享状态
- 使用多个线程模拟 Job 并发执行和完成通知
- 线程安全的 Worker 资源快照
- CTest 自动化测试
- 固定随机种子的可重复调度 benchmark

如果 FIFO 队首 Job 无法在任何 Worker 上运行，它会继续留在队列中，后续 Job 不会越过它。这保持了严格 FIFO，但也可能造成队头阻塞（head-of-line blocking）。

## 调度策略

### FirstFit

按照 Worker 注册顺序，选择第一个具有足够可用 CPU 和内存的 Worker。该策略简单，但结果依赖 Worker 顺序，也不会主动平衡负载或紧凑放置任务。

### BinPack

在所有满足要求的 Worker 中，优先选择放置 Job 后剩余 CPU 最少的 Worker；CPU 相同时选择剩余内存更少的 Worker。它倾向于紧凑放置任务，为后续任务保留较大的空闲 Worker。

### LeastAllocated

计算放置 Job 后的 CPU 和内存综合利用率，选择利用率最低的 Worker。它倾向于分散负载，但也可能让剩余资源分布在不同 Worker 上，从而产生放置碎片。

## 构建与运行

在项目根目录执行：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/heterosched
```

示例输出如下。线程运行顺序由系统调度决定，因此开始和结束日志的顺序可能不同；多个线程同时写入 `std::cout` 时，个别日志文本也可能交错。

```text
Worker 1 started Job 1
Worker 2 started Job 2
Worker 2 finished Job 2
Worker 1 finished Job 1
Completed Job 1 and 2 and released its resources
```

## 运行测试

```bash
ctest --test-dir build --output-on-failure
```

当前测试覆盖：

- 三种调度策略的 Worker 选择
- 不可调度的 FIFO 队首 Job 是否继续等待
- 等待中和运行中 Job 数量
- 重复 `jobId` 拒绝逻辑
- 多线程并发完成 Job
- 资源分配和释放后的 Worker 快照

## Benchmark

使用 Release 构建后运行：

```bash
./build/scheduler_benchmark
```

Benchmark 使用固定随机种子生成 100 个 Job，并让三种策略处理完全相同的输入。每种策略重复运行 10,000 次，每次都创建全新的 Scheduler 和 Worker。

计时只覆盖 `scheduleNext()` 调用，不包含 Scheduler 创建、Job 提交、快照复制、指标计算或输出。报告的时间是每次调度尝试的平均纳秒耗时；一次实验的尝试次数包含成功调度调用以及最后一次用于结束 FIFO 循环的失败调用。

该实验模拟批量放置，因此成功调度的 Job 不会立即释放资源。它衡量：

- 成功调度和剩余等待的 Job 数量
- 集群加权 CPU 利用率
- 集群加权内存利用率
- 每次调度尝试的平均耗时
- 调度停止原因

停止原因包括：

- `AllScheduled`：所有 Job 均已调度。
- `InsufficientCapacity`：集群总剩余 CPU 或内存不足。
- `ResourceFragmentation`：集群总剩余资源足够，但没有单个 Worker 能同时满足 Job。
- `UnexpectedFailure`：存在能够运行 Job 的 Worker，但调度仍然失败。

示例输出：

```text
Policy            Scheduled      Waiting        CPU Util (%)   Memory Util (%)   Avg Schedule (ns)   StopReason
FirstFit          11             89             92.86          76.46             ...                 InsufficientCapacity
BinPack           11             89             92.86          76.46             ...                 InsufficientCapacity
LeastAllocated    9              91             78.57          64.05             ...                 ResourceFragmentation
```

平均耗时会随硬件、系统负载和编译器变化；单次本地结果不应被视为严格的性能结论。

## 当前限制

- Scheduler 和 Worker 仍在同一进程中
- Scheduler 使用一把粗粒度 mutex，尚未研究锁竞争
- Job 只是模拟的资源请求，不执行真实工作负载
- 没有长期运行的 Worker 线程或自动调度循环
- 没有持久化和已完成 Job 历史
- 没有网络通信、Worker 注册、心跳和故障检测
- 没有 GPU、GPU 显存或 NPU 资源模型
- Benchmark 使用小规模合成负载，不代表生产环境性能

## 后续计划

- 将 Scheduler 和 Worker 拆分为独立进程
- 使用基础 TCP socket 实现 Worker 注册和消息通信
- 添加 Worker 心跳与故障检测
- 添加 Job 失败重试和重新调度
- 扩展 GPU、GPU 显存和 NPU 资源约束
- 增加更完整的调度实验和指标
