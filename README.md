# HeteroSched

HeteroSched 是一个使用 C++ 逐步构建轻量级资源调度器的学习项目。当前版本专注于理解单进程环境中的调度决策和资源管理，为后续引入并发执行与网络通信打下基础。

## 当前架构

```text
                 main
                   |
                   v
              Scheduler
              /   |   \
             v    v    v
         Worker Worker Worker
```

Scheduler 管理 FIFO 等待队列、多个 Worker，以及当前正在运行的 Job 记录。每个Worker 负责记录自己的 CPU 和内存总量与可用量。

## 已实现功能

- 使用 C++17 和 CMake 构建项目
- CPU 核数与内存资源模型
- 包含资源需求的最小 Job 模型
- Worker 资源可行性检查
- 资源分配与释放
- FIFO 任务提交队列
- 运行中任务记录与任务完成处理
- 在 Scheduler 生命周期内拒绝重复的 `jobId`
- FirstFit、BinPack 和 LeastAllocated 三种调度策略
- 使用 CTest 运行自动化调度器测试

如果队首 Job 无法在任何 Worker 上运行，它会继续留在队列中。这维持了严格的FIFO 顺序，但也可能造成队头阻塞（head-of-line blocking）。

## 调度策略

### FirstFit

按照 Worker 的添加顺序，选择第一个拥有足够可用 CPU 和内存的 Worker。该策略简单且快速，但结果依赖 Worker 的顺序，也不会主动平衡负载或紧凑放置任务。

### BinPack

在所有满足资源要求的 Worker 中，选择放入 Job 后剩余 CPU 核数最少的 Worker；如果剩余 CPU 相同，则比较剩余内存。该策略倾向于把任务紧凑地放到较小或使用率较高的 Worker 上，从而为未来的任务保留较大的连续空闲资源。

### LeastAllocated

选择放入 Job 后 CPU 与内存综合利用率最低的 Worker。该策略会把任务分散到负载较低的 Worker 上，但可能导致空闲资源分散在不同节点中，产生资源碎片。

## 构建与运行

在项目根目录执行：

```bash
cmake -S . -B build
cmake --build build
./build/heterosched
```

预期输出：

```text
PASS: job lifecycle and resource release work correctly
```

## 运行测试

```bash
ctest --test-dir build --output-on-failure
```

当前测试覆盖：

- 三种调度策略的 Worker 选择结果
- 不可调度的 FIFO 队首 Job 是否被保留
- 等待中和运行中 Job 的数量
- 重复 `jobId` 的拒绝逻辑

## 当前限制

- 仅支持单进程、单线程执行
- Job 目前只是模拟的资源请求，不会执行真实工作负载
- 没有持久化和已完成任务历史记录
- 没有网络通信、心跳和故障检测
- 没有 GPU 或 NPU 资源模型
- 暂无基准测试结果

## 后续计划

- 引入基础的 Worker 并发执行
- 添加可重复的调度基准测试和资源利用率测量
- 将 Scheduler 和 Worker 拆分为独立进程
- 添加 Worker 注册、心跳和故障检测
- 扩展资源模型，支持 GPU 和 NPU 约束
