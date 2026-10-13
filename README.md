# HeteroSched

HeteroSched 是一个使用 C++17 逐步构建的轻量级资源调度器学习项目。已完成单进程资源调度，并实现基础 TCP Worker 注册：Worker 客户端发送资源信息，Scheduler 服务端接收、解析、校验并注册 Worker。

当前进度：Phase 1 单进程调度和 Phase 2.1 基础 TCP Worker 注册已完成。网络版目前只演示注册流程；网络 Job 分发、真实任务执行、心跳和 GPU 调度属于后续计划。

## 当前架构

### 单进程调度演示

`heterosched` 在同一进程内管理 Worker 和 Job，并通过线程模拟执行与完成：

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

### TCP 注册演示

```text
worker_client 进程
    |
    | TCP: REGISTER_WORKER 1 8 16384\n
    v
scheduler_server 进程
    receiveLine()
        |
    parseRegistration()
        |
    Scheduler::addWorker()
```

客户端发送 Worker ID 和资源信息。服务端接收完整的一行，解析并校验内容，然后将 Worker 的资源状态保存到 Scheduler。当前客户端尚未接收或执行 Job。

## 已实现功能

- C++17 和 CMake 构建
- CPU 核数与内存资源模型
- 最小 Job 模型：Job ID 和资源需求
- Worker 总资源与可用资源管理
- Worker 资源检查、分配和释放
- FIFO Job 等待队列
- 运行中 Job 记录和完成处理
- Scheduler 生命周期内拒绝重复 `jobId`
- Worker 输入校验和重复 `workerId` 拒绝
- FirstFit、BinPack 和 LeastAllocated 调度策略
- 使用 mutex 保护 Scheduler 共享状态
- 使用多个线程模拟 Job 并发执行和完成通知
- 线程安全的 Worker 资源快照
- Scheduler 和注册解析的 CTest 自动化测试
- 固定随机种子的可重复调度 benchmark
- 独立的 Scheduler 服务端和 Worker 客户端
- Linux / WSL2 下的阻塞式 TCP 通信
- 循环发送完整消息，处理部分发送和 `EINTR`
- 按换行符接收完整消息，限制正文最多 128 字节
- 独立的 Worker 注册消息解析与校验函数

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
```

运行单进程调度演示：

```bash
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

### 运行 TCP Worker 注册演示

构建完成后，在项目根目录打开两个终端。先在终端一启动服务端：

```bash
./build/scheduler_server
```

服务端监听 `127.0.0.1:9000`，输出监听日志后等待连接。再在终端二启动客户端：

```bash
./build/worker_client 1 8 16384
```

三个命令行参数依次为 Worker ID、CPU 核数和内存 MB。客户端只检查参数数量，直接将参数文本拼入注册消息；整数转换、资源约束和 ID 校验由服务端完成。上述命令注册 Worker `1`，CPU 为 `8` 核、内存为 `16384 MB`。客户端输出：

```text
Connected to Scheduler at 127.0.0.1:9000
Registration message sent
Registration success
```

服务端输出：

```text
Listening on 127.0.0.1:9000
Client connected
Received: REGISTER_WORKER 1 8 16384
Registered Worker 1, worker count: 1
```

两个程序在处理一次注册后退出，每次演示都需要重新启动服务端。`Registration message sent` 只表示消息已提交给本地 socket；服务端注册成功后回复 `REGISTER_OK`，客户端收到确认后打印 `Registration success`。解析或注册被拒绝时，服务端回复 `REGISTER_ERROR`，客户端打印 `Registration failed` 并以非零状态退出。

## 注册消息协议

客户端将三个参数用空格拼接，发送以下文本消息：

```text
REGISTER_WORKER <workerId> <cpuCores> <memoryMB>\n
```

`\n` 表示实际的换行字节。TCP 提供字节流，一次接收不保证得到完整消息；服务端持续读取直到换行，再将不含换行的正文交给解析函数。

| 项目 | 当前规则 |
|---|---|
| 地址 | `127.0.0.1:9000`，仅本机通信 |
| 消息边界 | 换行符 `\n` |
| 正文长度 | 最多 128 字节，不含结束符 |
| Worker ID | 非负 `int`，允许 `0` |
| CPU 核数 | 正 `int` |
| 内存容量 | 正 `int`，单位为 MB |
| 命令名 | 必须为 `REGISTER_WORKER`，区分大小写 |
| 非法输入 | 拒绝缺失字段、非整数、整数溢出、非法资源和额外非空白内容 |
| 重复 ID | 同一个 Scheduler 实例内拒绝重复 Worker ID |

当前解析器允许多余空白和数字字段紧邻正号的写法。以下正文均解析为 ID `1`、CPU `8`、内存 `16384 MB`：

```text
REGISTER_WORKER 1+8+16384
REGISTER_WORKER 1 8+16384
REGISTER_WORKER 1+8 16384
```

这里的正号属于整数的符号，不执行加法运算。客户端使用空格分隔三个命令行参数。

若连接在换行前结束，服务端报告 `Incomplete message`；正文超过长度限制时报告 `message too long`；内容无效时报告 `Invalid registration message`。服务端关闭 socket 并以非零状态退出。

## 运行测试

```bash
ctest --test-dir build --output-on-failure
```

CTest 包含两个测试程序，也可以分别运行：

```bash
./build/scheduler_tests
./build/registration_tests
```

`scheduler_tests` 覆盖：

- 三种调度策略的 Worker 选择
- 不可调度的 FIFO 队首 Job 是否继续等待
- 等待中和运行中 Job 数量
- 重复 `jobId` 拒绝逻辑
- Job 和 Worker 输入校验、重复 Worker ID 拒绝及两种 ID 的独立命名空间
- 多线程并发完成 Job
- 资源分配和释放后的 Worker 快照

`registration_tests` 覆盖：

- 正常消息解析后的 Worker ID、总资源和初始可用资源
- Worker ID 为 `0` 的合法消息
- `REGISTER_WORKER 1+8+16384` 的解析结果
- 空消息、错误命令、缺失或多余字段
- 负 ID、非正资源、非整数、数字后缀和内存整数溢出

当前 CTest 测试调度核心和注册解析，不自动启动 TCP 服务端与客户端。网络收发可按上述双终端步骤手动验证。

## Benchmark

使用 Release 构建后运行：

```bash
./build/scheduler_benchmark
```

Benchmark 测量单进程调度性能，不包含 TCP 通信。它使用固定随机种子生成 100 个 Job，并让三种策略处理完全相同的输入。每种策略重复运行 10,000 次，每次都创建全新的 Scheduler 和 Worker。

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

- TCP 服务端只接受一个客户端，处理一条注册消息后退出
- 注册状态仅保存在内存中，服务端退出后消失
- 地址和端口固定在代码中，客户端 Worker ID 和资源信息通过命令行传入
- 尚未实现网络 Job 分发和执行结果回报
- Scheduler 使用一把粗粒度 mutex，尚未研究锁竞争
- Job 只是模拟的资源请求，不执行真实工作负载
- 没有长期运行的 Worker 线程或自动调度循环
- 没有持久化和已完成 Job 历史
- 没有心跳、故障检测和 Job 重试
- 没有 GPU、GPU 显存或 NPU 资源模型
- Benchmark 使用小规模合成负载，不代表生产环境性能

## 后续计划

- 完善注册失败原因反馈和可配置的连接地址
- 扩展多个 Worker 的连接管理
- 实现网络 Job 分发和任务执行结果回报
- 添加 Worker 心跳与故障检测
- 添加 Job 失败重试和重新调度
- 扩展 GPU、GPU 显存和 NPU 资源约束
- 增加更完整的调度实验和指标
