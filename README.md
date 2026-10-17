# HeteroSched

HeteroSched 是一个使用 C++17 逐步构建的轻量级资源调度器学习项目。已完成单进程资源调度，并通过 TCP 支持 Worker 注册、Job 提交、任务领取和完成上报。保留原阻塞式服务端，并新增独立的 Linux LT epoll 服务端。Worker 使用短连接轮询，自动领取任务、模拟执行并上报完成；Scheduler 维护队列、运行记录和 Worker 资源。

当前进度：Phase 1 单进程调度已完成；Phase 2 已实现 Worker 自动领取、模拟执行、完成上报的网络闭环，并保留手动调度与完成接口。提交成功表示入队，调度成功表示资源已预留，领取成功表示派发记录已消费，完成成功表示资源已释放。目前执行使用 500 ms 的休眠模拟，真实工作负载、心跳、故障恢复和 GPU 调度属于后续计划。

epoll 第一版已通过连接生命周期、并发请求和缓冲区测试；历史阻塞版网络压测基线已保存。两版现在共享请求日志格式和 `--quiet` 模式，可通过一键对比工具在相同条件下重测，结果和限制见 Benchmark 部分。

## 目录结构

公共头文件、实现和测试按同一组职责分类；可执行程序的入口统一放在 `apps/`。

```text
HeteroSched/
├── apps/                       # 六个可执行程序入口
│   ├── demo_main.cpp            # 单进程演示，对应 heterosched
│   ├── scheduler_server.cpp
│   ├── scheduler_server_epoll.cpp
│   ├── worker_client.cpp
│   ├── job_client.cpp
│   └── scheduler_client.cpp
├── include/                    # 公共接口
│   ├── core/                   # Resource、Job、Worker、Scheduler
│   ├── net/                    # TCP 字节流收发
│   └── protocol/               # 注册、提交、完成和调度协议
├── src/                        # 与 include/ 分类对应的实现
│   ├── core/
│   ├── net/
│   └── protocol/
├── tests/                      # 按被测模块分组
│   ├── core/
│   ├── net/
│   └── protocol/
├── benchmarks/
│   ├── scheduler_benchmark.cpp
│   ├── tcp_benchmark.py         # TCP 服务压测，Python 标准库
│   └── results/                # 保存的原始请求耗时与测量环境
├── CMakeLists.txt
└── README.md
```

头文件引用包含模块路径，例如 `#include "core/job.h"`、`#include "net/tcp_io.h"` 和 `#include "protocol/scheduler_protocol.h"`。`Job` 是纯数据结构，直接定义在 `include/core/job.h` 中，不需要空的实现文件。

CMake 构建三个可复用库：`heterosched_core` 管理领域状态与调度，`heterosched_net` 提供 socket 收发，`heterosched_protocol` 依赖 core 并负责协议处理。`apps/` 按需链接这些库；单进程演示和 benchmark 只依赖 core，Job 客户端只依赖 net，Worker 客户端依赖 protocol 和 net。可执行文件名和下面的运行命令保持一致。

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

### 阻塞版 TCP 请求处理

```text
worker_client / job_client / scheduler_client
                     |
                     | 一条文本请求
                     v
scheduler_server 进程
    accept() 循环
        |
    handleClient(clientFd, scheduler)
        receiveLine()
            | 按命令分流
        processSchedulerRequest()
            +-- REGISTER_WORKER -> parseRegistration() -> addWorker()
            +-- SUBMIT_JOB      -> parseJobSubmission() -> submitJob()
            +-- SCHEDULE_NEXT   -> 校验无参数          -> scheduleNext()
            +-- FETCH_JOB       -> 校验 Worker ID     -> fetchJob()
            +-- COMPLETE_JOB    -> parseJobCompletion()-> completeJob()
            +-- 未知命令        -> REQUEST_ERROR UNKNOWN_COMMAND
        sendAll() 发送处理结果
        |
    close(clientFd) -> 回到 accept()
```

每条连接只处理一条请求。Job 客户端和控制客户端每次启动只发一个请求；Worker 客户端注册成功后长期运行，每次领取与完成上报都重新建立短连接。`main()` 拥有监听 socket 和 Scheduler，负责循环接受连接、配置接收超时及关闭客户端 socket。`handleClient()` 只接收一次完整行，再调用 `processSchedulerRequest()`。协议处理函数位于 `src/protocol/scheduler_protocol.cpp`，按精确命令名分流，校验后调用 Scheduler 并返回带换行的响应；网络入口负责发送结果。请求错误只结束该连接，服务端继续接受下一个连接。

Scheduler 创建在 `accept()` 循环外，五类请求共享同一个服务端的内存状态。网络服务端与单进程演示使用各自独立的 Scheduler 实例。`submitJob()` 只负责入队和 Job ID 去重；手动调度或 Worker 领取请求可触发资源预留；`COMPLETE_JOB` 回收资源。

任务经过三个阶段：

```text
waitingJobs_（未分配）
    -> 全局策略选择 Worker，扣减资源
runningJobs_ + pendingDispatches_[workerId]（已预留，待领取）
    -> 对应 Worker 领取，移除 pending 中的 Job ID
runningJobs_（已交付，等待完成）
    -> COMPLETE_JOB 释放资源，删除运行记录
```

`pendingDispatches_` 按 Worker 保存 FIFO Job ID 队列，Job 内容仍保存在 `runningJobs_`。它从空表开始，在调度成功时按需加入记录，无需预先扫描初始化。`fetchJob()` 在同一把 mutex 下先检查 Worker 是否存在，再优先领取自己的待派发任务；若没有，只尝试调度等待队首这一个 Job，然后再次领取自己的任务。全局调度可能把任务分给其他 Worker，此时调用方返回 `NO_JOB`，目标 Worker 后续领取。未知 Worker 不触发调度。

领取不再次扣资源，也不删除运行记录；重复领取不会返回同一条派发记录。手动完成尚未领取的 Job 会同步移除其待派发 ID，避免下次发送已完成任务。`scheduleNextLocked()` 和 `takePendingJobLocked()` 供已持锁的调用方使用，避免嵌套锁定非递归 mutex。

服务端对每条完整请求输出 `Received`、生成的 `Response`、等待/运行数量以及各 Worker 的可用 CPU 和内存。状态在响应发送之前修改和打印；这些日志不证明客户端已经收到响应。

### LT epoll TCP 请求处理

`scheduler_server_epoll` 使用同一份 `Scheduler` 和 `processSchedulerRequest()`，通过 `EpollServer` 管理网络。监听 socket 与 `accept4()` 得到的客户端 socket 均设置 `SOCK_NONBLOCK | SOCK_CLOEXEC`，`epoll_create1()` 创建事件实例，监听队列 backlog 默认 1，可通过 `--backlog` 配置。两版都在 bind 前设置 `SO_REUSEADDR`，便于在同一端口依次重启实验进程。网络事件循环单线程运行；业务状态继续通过原有 Scheduler API 访问。

每个连接保存以下状态：

| 字段 | 用途 |
|---|---|
| `fd` | 由 `UniqueFd` 独占并自动关闭 socket |
| `lastActivity` | 接受连接或最近一次成功收发字节的单调时钟时间，用于可选的空闲清理 |
| `input` | 保存尚未解析的字节，包括未完成的一行和输出背压时暂存的完整行 |
| `output` / `outputOffset` | 保存响应及已发送位置，部分发送后从原位置继续 |
| `readClosed` | `recv()` 已返回 EOF，对端不再发送；仍可发送待处理响应 |
| `closing` | 帧超过长度上限，发完此前合法请求的响应后关闭 |
| `interests` | 当前订阅的事件，避免无变化的 `epoll_ctl MOD` |

`epoll_wait()` 返回后，监听事件触发非阻塞 accept；客户端可读时循环接收并按换行拆帧，完整行立即交给协议处理函数。`EAGAIN/EWOULDBLOCK` 保留连接和缓冲区，`EINTR` 重试，其他收发错误结束该连接。正文 128 字节上限由阻塞版与 epoll 版共享常量；多个完整行可以一起到达，某一行也可以分多次到达。

发送同样使用非阻塞 `send(..., MSG_NOSIGNAL)`。只有存在待发送字节时才订阅 `EPOLLOUT`，发完后取消，避免 socket 一直可写造成空转。输出达到 64 KiB 高水位时暂停 `EPOLLIN/EPOLLRDHUP`；输出腾出空间后，先解析已在用户态缓存中的完整行，再恢复读取。高水位允许最后一个响应越过阈值，当前协议的单条响应长度有界。

LT 模式下，每个连接每轮最多接收 64 KiB、发送 64 KiB，每轮最多接受 64 个连接；未完成的就绪操作会继续获得通知，避免持续活跃的一个客户端占用整轮循环。`EPOLLRDHUP` 触发读完剩余请求；读到 EOF 后发完完整请求的响应，丢弃不完整的尾行，再关闭连接。错误或完整挂断事件按连接清理。连接用递增代号关联 epoll 事件，避免已关闭 fd 被复用后，旧事件误操作新连接。

新入口支持同一连接上连续请求，每条请求对应一条按序响应；发完响应后继续等待下一条请求。原有客户端读取一行后关闭连接，因此其短连接请求方式仍可使用。一个客户端发不完一行或暂时不读响应时，其他客户端可以继续处理。协议状态修改仍发生在响应发送前，响应丢失不会回滚调度状态，也没有新增重试保证。

Linux 接口语义参考 [epoll(7)](https://man7.org/linux/man-pages/man7/epoll.7.html) 和 [epoll_ctl(2)](https://man7.org/linux/man-pages/man2/epoll_ctl.2.html)。

### epoll 连接空闲超时

慢连接隔离保证其他连接能继续推进，空闲连接本身仍占用 fd 和缓冲区。epoll 入口支持可选的 `--idle-timeout-ms`，范围为 `0..2147483647`，默认 `0` 关闭；例如连续 5 秒没有成功收发字节时关闭连接：

```bash
./build/scheduler_server_epoll --port 9000 --backlog 128 --idle-timeout-ms 5000 --quiet
```

每条连接在 accept 时初始化 `lastActivity`；只有 `recv()` 或 `send()` 返回正字节数才续期，可读/可写通知、`EAGAIN` 和 EOF 不算活动。使用 `std::chrono::steady_clock`，避免系统日期调整影响截止时间。事件循环在等待前后清理过期连接；最近的截止时间限制 `epoll_wait()` 的等待时长，即使没有新网络事件也能触发清理。剩余毫秒向上取整，避免不足 1 ms 时以零超时反复轮询。同批返回的事件中，已到期连接先被关闭，其旧事件按连接代号跳过。

当前采用每轮 O(N) 扫描连接表，不新增线程或定时器结构；关闭超时时不扫描连接表或采样活动时间。连接规模增大后可根据测量考虑堆或时间轮，但本轮没有测量扫描开销，不声称吞吐提升。毫秒等待和清理受操作系统调度及同步协议处理影响，不保证精确在截止时刻执行。

这是连接空闲超时，不是整条请求的总截止时间。客户端在间隔内持续补字节仍能续期；它不能独立防止大量慢连接长期占用资源。到期直接移除 epoll 注册并释放连接：不完整输入不执行，尚未发送的用户态输出丢弃，不发送额外协议错误。已处理的注册、提交或分配不会回滚，Worker 不会注销，Job 资源也不会因 TCP 超时自动释放；故障恢复需要独立设计。接收半关闭但仍有输出的连接同样按成功发送字节续期，停滞输出最终可以被清理。

阻塞入口不支持该参数，传入会报错，仍沿用单次 `recv()` 的 5 秒超时。原对比工具不传此参数，默认关闭空闲清理；历史 benchmark 和原始数据保留，不把启用超时后的结果与旧条件直接比较。

### epoll 连接数量上限

空闲超时不能回收持续收发数据的连接。epoll 入口新增 `--max-connections`，范围为 `0..2147483647`，默认 `0` 不限制；例如最多保留 128 条客户端连接，并回收连续 5 秒没有收发字节的连接：

```bash
./build/scheduler_server_epoll --port 9000 --backlog 128 \
  --max-connections 128 --idle-timeout-ms 5000 --quiet
```

`acceptReady()` 在 `accept4()` 成功后检查连接表大小。达到上限时，由临时 `UniqueFd` 立即关闭新 socket，不创建连接状态或缓冲区、不注册客户端 epoll 事件、不读取请求，也不调用协议 handler。没有额外的 BUSY 响应；客户端可能已完成握手，随后观察到 EOF 或 TCP reset，不能将其理解为 `connect()` 一定失败。原 Worker 客户端遇到通信失败仍会退出，不自动重试。

达到上限不会关闭已有连接；持久连接仍可继续提交请求。半关闭但输出尚未发送完毕的连接仍占名额，正常结束、reset、超长帧清理或空闲超时后才释放。计数直接使用 `connections_.size()`，无需维护易失配的额外计数器。每次接受或拒绝都计入现有每轮 64 次 accept 预算；持续就绪的监听 socket 仍以 LT 方式通知。相同批次中若先处理新连接、后处理旧连接关闭，新连接可能先被拒绝；名额按检查时的实际状态决定，不保证排队公平或自动重试。

| 配置 | 限制对象 |
|---|---|
| `--backlog` | 内核中等待应用 accept 的已建立连接队列，请求值还受内核设置影响 |
| `--max-connections` | 应用保留的客户端连接状态数量 |
| `--idle-timeout-ms` | 已接受连接连续没有成功收发字节的时间 |

本轮采用接受后立即关闭的策略，没有暂停监听事件来等待名额。连接上限检查为 O(1)，但拒绝仍有 accept/close 成本，不是请求速率限制；本轮没有测量吞吐或拒绝速率。上限不包含监听 socket、epoll、日志等 fd，拒绝过程中也会短暂多持有一个新 socket，因此需要给进程 fd 限制留出余量。它不保证系统 fd 耗尽后的恢复，`accept4()` 的 `EMFILE/ENFILE` 仍按现有错误路径结束服务端；也不限制 Worker/Job 总数或调度队列长度。

该参数仅用于 epoll 入口，阻塞入口传入会报错。对比工具默认不设置上限，原始 benchmark 条件和历史数据保留。

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
- Scheduler、请求/响应解析、协议状态转换和 socket 收发的 CTest 自动化测试
- 固定随机种子的可重复调度 benchmark
- 独立的 Scheduler 服务端、Worker 客户端、Job 提交客户端和调度控制客户端
- Linux / WSL2 下的阻塞式 TCP 通信
- 循环发送完整消息，处理部分发送和 `EINTR`
- 按换行符接收完整消息，限制正文最多 128 字节
- 独立的 Worker 注册消息解析与校验函数
- 服务端持续监听并串行处理多个客户端注册
- 同一服务端实例内保留注册状态并拒绝重复 Worker ID
- 注册结果确认，以及 `INVALID_MESSAGE` / `WORKER_REJECTED` 错误码反馈
- 单次请求失败后关闭该连接，继续接受后续请求
- 服务端请求连接设置 5 秒接收等待超时，超时后关闭连接并继续服务
- 按请求命令分流，未知命令返回 `REQUEST_ERROR UNKNOWN_COMMAND`
- 网络 Job 提交、提交结果确认，以及 `INVALID_MESSAGE` / `JOB_REJECTED` 错误码反馈
- 无 Worker 时合法 Job 也可入队，Job ID 与 Worker ID 独立去重
- 手动网络调度返回 Job/Worker 分配结果，失败保留 FIFO 队首
- 手动完成通知释放资源，拒绝等待中、未知或重复完成的 Job
- 调度控制客户端校验响应字段及操作类型，服务端输出状态和资源快照
- Worker 按秒轮询领取本节点任务，校验 `RUN_JOB` 响应并模拟执行 500 ms
- Worker 自动完成上报、确认后继续领取，每个 Worker 进程串行执行任务
- 每个 Worker 的 FIFO 待派发队列、单次领取及手动完成后的派发清理
- 独立的单线程 LT epoll 服务端，非阻塞 accept/recv/send
- 每连接输入输出缓冲、部分发送续传、输出背压和每轮 I/O 预算
- 单连接多条请求、半关闭后响应发送及断开清理
- epoll 可配置连接空闲超时，成功收发续期、到期释放 fd 和连接缓冲区，默认关闭
- epoll 可配置客户端连接数量上限，超额连接立即关闭、已有连接继续服务，默认不限制
- 真实 C++ epoll 服务端集成测试，验证慢客户端隔离与并发领取/完成记账

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

### 运行 Worker 自动执行闭环

打开三个终端，在项目根目录操作。终端一启动全新的服务端：

```bash
./build/scheduler_server
```

服务端监听 `127.0.0.1:9000`。终端二启动 Worker：

```bash
./build/worker_client 1 2 4096
```

参数依次为 Worker ID、CPU 核数、内存 MB。客户端只检查参数数量，将原始参数发给服务端解析和校验。注册成功后，Worker 保持运行，空闲时每隔 1 秒发起一次 `FETCH_JOB`，不会退出：

```text
Connected to Scheduler at 127.0.0.1:9000
Registration message sent
Registration success
No Job available; retrying in 1s
```

`Registration message sent` 只表示发送成功；收到 `REGISTER_OK` 才表示注册成功。终端三提交两个任务：

```bash
./build/job_client 101 2 4096
./build/job_client 102 1 1024
```

Job 参数依次为 ID、CPU 核数、内存 MB。成功时 Job 客户端打印 `Submit success` 并退出，表示任务已入队。无需运行 `scheduler_client next` 或手动完成，Worker 随后输出：

```text
Worker 1 started Job 101 (CPU=2, memoryMB=4096)
Worker 1 finished Job 101
Job 101 completion accepted
Worker 1 started Job 102 (CPU=1, memoryMB=1024)
Worker 1 finished Job 102
Job 102 completion accepted
No Job available; retrying in 1s
```

任务执行用 500 ms 休眠模拟，所声明的 CPU 和内存只用于调度记账，不会实际占用相应资源。服务端处理完成上报后，最终恢复到：

```text
State: workers=1, waiting=0, running=0
Worker 1: available CPU=2, memoryMB=4096
```

领取响应接收后连接就关闭，休眠和执行均发生在 Worker 本地，因此服务端可以继续接收其他请求。可在第四个终端启动 `./build/worker_client 2 2 4096` 验证两个 Worker；FirstFit 按注册顺序选节点，不保证任务平均分配。每个 Worker 进程一次只执行一个任务，调度器仍按声明的资源管理容量。

实验结束时，对 Worker 和服务端分别按 `Ctrl+C`。Worker 退出不会注销，服务端重启后状态清空；同一服务端实例内重复启动同一 Worker ID 会注册失败。

### 运行 epoll 服务端

完成相同的 CMake 构建后，可在原服务端终端按 Ctrl+C 退出阻塞版，再启动：

```bash
./build/scheduler_server_epoll
```

默认同样监听 `127.0.0.1:9000`，可按上面的步骤使用原 Worker、Job 和控制客户端。两种服务端是独立进程，各自拥有内存状态；切换后需要重新注册 Worker、提交 Job。两版默认都输出请求、响应和调度状态，使用 `--quiet` 关闭逐请求日志及普通客户端收发错误日志，仅保留启动地址和致命/配置错误；建议 epoll 服务端在压测时使用 quiet 模式，避免同步日志写入占用事件循环。

如果 9000 端口正在使用，可单独启动一个空状态的测试实例：

```bash
./build/scheduler_server_epoll --port 19000
```

两种服务端均支持 `--port`、`--quiet` 和 `--backlog`，例如 `./build/scheduler_server --port 19000 --backlog 128 --quiet`。backlog 要求正 `int`，默认仍为 1，便于复现旧基线。benchmark 支持 `--port 19000`；原有三个 C++ 客户端的端口仍固定为 9000。`--port 0` 可由系统分配空闲端口，实际地址会打印到启动日志，自动化测试使用此方式避免端口冲突。服务端均仅绑定 loopback。

### 验证输入与错误处理

保持上面的服务端运行，在其他终端执行：

```bash
./build/worker_client -1 4 8192    # Registration failed: INVALID_MESSAGE
./build/worker_client 1 4 8192     # Registration failed: WORKER_REJECTED
./build/job_client 101 2 4096     # Submit failed: JOB_REJECTED（已完成 ID 仍保留）
./build/job_client -1 2 4096      # Submit failed: INVALID_MESSAGE
./build/job_client 103 0 4096     # Submit failed: INVALID_MESSAGE
./build/job_client 103 abc 4096   # Submit failed: INVALID_MESSAGE
```

非法请求不修改队列或资源，客户端以非零状态退出，服务端继续服务。没有 Worker 时，合法 Job 也能入队；Job ID 与 Worker ID 可相同，两者独立去重。

### 手动调度与完成实验

仍保留 `scheduler_client next` 和 `scheduler_client complete <jobId>`。为了观察固定的手动状态转换，先启动全新的服务端，再运行 `./build/worker_client 1 2 4096`；看到注册成功后，在 Worker 终端按 `Ctrl+C`，使其停止轮询。此时服务端仍保留 Worker 资源记录。然后依次执行：

```bash
./build/job_client 101 2 4096
./build/job_client 102 1 1024
./build/scheduler_client next
./build/scheduler_client next
./build/scheduler_client complete 101
./build/scheduler_client next
./build/scheduler_client complete 101
./build/scheduler_client complete 102
./build/scheduler_client next
```

| 操作 | 响应 | 等待 / 运行 | Worker 可用 CPU / 内存 MB |
|---|---|---|---|
| 提交两个 Job | `SUBMIT_OK` | 2 / 0 | 2 / 4096 |
| 第一次 `next` | `SCHEDULE_OK 101 1` | 1 / 1 | 0 / 0 |
| 第二次 `next` | `SCHEDULE_NONE` | 1 / 1 | 0 / 0 |
| 完成 101 | `COMPLETE_OK` | 1 / 0 | 2 / 4096 |
| 再次 `next` | `SCHEDULE_OK 102 1` | 0 / 1 | 1 / 3072 |
| 重复完成 101 | `COMPLETE_ERROR COMPLETION_REJECTED` | 0 / 1 | 1 / 3072 |
| 完成 102 | `COMPLETE_OK` | 0 / 0 | 2 / 4096 |
| 最后 `next` | `SCHEDULE_NONE` | 0 / 0 | 2 / 4096 |

手动分配的任务也会进入待派发队列；在领取前手动完成会清理该派发记录。运行中的 Worker 会自动领取和完成，混用手动控制时，观察到的状态取决于请求顺序。完成未知、等待中或已完成的 Job 均被拒绝；重复完成不会增加资源。完成本身不自动调度下一项，后续 Worker 轮询或 `next` 才触发调度。

## TCP 消息协议

Worker 和 Job 注册/提交时将三个参数用空格拼接；Worker 还发送领取和完成请求，控制客户端保留手动调度与完成：

```text
REGISTER_WORKER <workerId> <cpuCores> <memoryMB>\n
SUBMIT_JOB <jobId> <cpuCores> <memoryMB>\n
SCHEDULE_NEXT\n
FETCH_JOB <workerId>\n
COMPLETE_JOB <jobId>\n
```

`\n` 表示实际的换行字节。TCP 提供字节流，一次接收不保证得到完整消息；服务端持续读取直到换行，再将不含换行的正文交给解析函数。

| 项目 | 当前规则 |
|---|---|
| 地址 | `127.0.0.1:9000`，仅本机通信 |
| 消息边界 | 换行符 `\n` |
| 正文长度 | 最多 128 字节，不含结束符 |
| Worker ID | 非负 `int`，允许 `0` |
| Job ID | 非负 `int`，允许 `0` |
| CPU 核数 | 正 `int` |
| 内存容量 | 正 `int`，单位为 MB |
| 命令名 | `REGISTER_WORKER`、`SUBMIT_JOB`、`SCHEDULE_NEXT`、`FETCH_JOB`、`COMPLETE_JOB`，区分大小写，精确匹配 |
| 控制请求参数 | `SCHEDULE_NEXT` 无参数；`COMPLETE_JOB` / `FETCH_JOB` 恰好一个非负整数 ID，拒绝额外内容 |
| 非法输入 | 拒绝缺失字段、非整数、整数溢出、非法资源和额外非空白内容 |
| 重复 ID | 同一个 Scheduler 实例内分别拒绝重复 Worker ID 和 Job ID；两种 ID 相互独立 |

注册和 Job 提交解析器均使用字符串流提取整数，允许多余空白和数字字段紧邻正号的写法。以下注册正文均解析为 ID `1`、CPU `8`、内存 `16384 MB`：

```text
REGISTER_WORKER 1+8+16384
REGISTER_WORKER 1 8+16384
REGISTER_WORKER 1+8 16384
```

Job 解析器同样允许 `SUBMIT_JOB 101+2+4096`，解析结果为 Job ID `101`、CPU `2`、内存 `4096 MB`。这里的正号属于整数的符号，不执行加法运算。两个客户端使用空格分隔三个命令行参数。

完成解析器允许 `COMPLETE_JOB +101` 和多余空白，但只接受一个完整整数，`COMPLETE_JOB 101+102` 会被拒绝。`FETCH_JOB` 的 ID 采用相同规则，例如接受 `FETCH_JOB +1`，拒绝 `FETCH_JOB 1+2`。

服务端响应同样以换行符结束：

| 响应 | 含义 |
|---|---|
| `REGISTER_OK\n` | Worker 已成功加入 Scheduler |
| `REGISTER_ERROR INVALID_MESSAGE\n` | 完整注册行的格式或参数不合法 |
| `REGISTER_ERROR WORKER_REJECTED\n` | 注册消息解析成功，但 `Scheduler::addWorker()` 拒绝注册，例如 Worker ID 重复 |
| `SUBMIT_OK\n` | Job 已加入 FIFO 等待队列 |
| `SUBMIT_ERROR INVALID_MESSAGE\n` | 完整提交行的格式或参数不合法 |
| `SUBMIT_ERROR JOB_REJECTED\n` | 提交消息解析成功，但 `Scheduler::submitJob()` 拒绝提交，例如 Job ID 重复 |
| `SCHEDULE_OK <jobId> <workerId>\n` | 调度成功，资源已分配，Job 已进入运行记录 |
| `SCHEDULE_NONE\n` | 队列为空、没有 Worker 或队首无法放置，本次没有调度结果 |
| `SCHEDULE_ERROR INVALID_MESSAGE\n` | 调度请求携带了额外参数 |
| `RUN_JOB <jobId> <cpuCores> <memoryMB>\n` | 已领取分配给该 Worker 的任务，资源之前已预留 |
| `NO_JOB\n` | 当前 Worker 暂无任务，不表示全局队列为空 |
| `FETCH_ERROR INVALID_MESSAGE\n` | 领取请求缺少 ID、ID 非法或含额外字段 |
| `FETCH_ERROR UNKNOWN_WORKER\n` | Worker 尚未注册，不触发调度 |
| `COMPLETE_OK\n` | 完成处理成功，资源已释放且运行记录已删除 |
| `COMPLETE_ERROR INVALID_MESSAGE\n` | 完成请求缺少 ID、ID 非法或含额外字段 |
| `COMPLETE_ERROR COMPLETION_REJECTED\n` | `completeJob()` 返回 false，例如 Job 未运行、已完成或释放失败 |
| `REQUEST_ERROR UNKNOWN_COMMAND\n` | 命令未被识别，例如 `SUBMIT_JOB_EXTRA` 或空行 |

`INVALID_MESSAGE` 目前不进一步区分非整数、负 ID 或非正资源。Worker 客户端按当前操作校验注册、领取和完成响应；`RUN_JOB` 必须恰好包含三个空白分隔的完整整数字段，ID 非负且资源为正，拒绝溢出、数字后缀和额外内容。仅精确的 `NO_JOB` 触发 1 秒后的下次轮询。错误响应、畸形响应或网络失败均使 Worker 以非零状态退出；完成确认必须为精确的 `COMPLETE_OK`。Job 客户端识别 `SUBMIT_OK` 和 `SUBMIT_ERROR ` 前缀，其他响应报错退出。

控制客户端按发出的操作识别响应。`next` 只接受合法 `SCHEDULE_OK`、精确的 `SCHEDULE_NONE` 或 `SCHEDULE_ERROR <错误码>`；`complete` 只接受精确的 `COMPLETE_OK` 或 `COMPLETE_ERROR <错误码>`。调度结果必须恰好包含两个空白分隔的完整非负整数字段，拒绝溢出、数字后缀、缺失或额外字段。错误响应必须恰好包含一个错误码；跨操作的响应也会被拒绝。

若连接在换行前结束，服务端报告 `Incomplete message`；正文超过长度限制时报告 `message too long`。这两类接收失败不发送业务结果，服务端关闭该客户端连接并继续等待后续连接。完整行命令被识别但内容无效时，按业务类型返回 `INVALID_MESSAGE`；重复 Worker 或 Job ID 则分别返回 `WORKER_REJECTED` 或 `JOB_REJECTED`。未知命令直接返回 `REQUEST_ERROR UNKNOWN_COMMAND`。收发失败也只结束当前请求；`accept()` 被 `EINTR` 打断时重试，其他 `accept()` 错误会关闭监听 socket 并结束服务端。

服务端在 `accept()` 成功后，对当前客户端 socket 设置 `SO_RCVTIMEO` 为 5 秒。接收调用等待期间没有数据可读并达到超时后，服务端报告 `Receive timed out`，关闭当前连接，不发送业务结果，然后继续接受连接。该配置限制单次 `recv()` 的阻塞时间，不是整条请求消息的总耗时限制；客户端持续缓慢发送字节时，整条消息仍可能耗时超过 5 秒。三个客户端等待响应时尚未设置接收超时。

状态修改先于响应发送。发送失败不会回滚注册、入队、分配或资源释放，也未缓存请求结果；调用方不能仅凭未收到响应判断操作是否发生。例如自动重试 `SCHEDULE_NEXT` 可能分配另一个 Job；`FETCH_JOB` 的响应丢失后，该任务的派发记录已移除，运行记录及资源预留仍保留。当前 Worker 只在明确收到 `NO_JOB` 时继续轮询，通信失败不自动重试。Worker 执行中崩溃也没有恢复机制。完成消息目前只携带 Job ID，不校验上报者身份，适用于本机受信任的学习演示。

## 运行测试

```bash
ctest --test-dir build --output-on-failure
```

安装 Python 3 时，CTest 包含十一个测试程序；没有 Python 时，八个 C++ 测试及应用仍可正常构建。可以分别运行：

```bash
./build/scheduler_tests
./build/job_dispatch_tests
./build/registration_tests
./build/tcp_io_tests
./build/epoll_server_io_tests
./build/job_submission_tests
./build/job_completion_tests
./build/scheduler_protocol_tests
python3 tests/net/tcp_benchmark_test.py
python3 tests/net/epoll_server_integration_test.py ./build/scheduler_server_epoll
python3 tests/net/tcp_comparison_test.py ./build
```

`scheduler_tests` 覆盖：

- 三种调度策略的 Worker 选择
- 不可调度的 FIFO 队首 Job 是否继续等待
- 等待中和运行中 Job 数量
- 重复 `jobId` 拒绝逻辑
- Job 和 Worker 输入校验、重复 Worker ID 拒绝及两种 ID 的独立命名空间
- 多线程并发完成 Job
- 资源分配和释放后的 Worker 快照

`job_dispatch_tests` 覆盖 Worker 领取闭环、未知 Worker 不触发调度、领取不重复分配资源、同一任务不重复领取、只调度队首一次、跨 Worker 派发、优先领取已有任务、严格 FIFO、三种策略复用、手动完成未领取任务的清理，以及并发领取不重复交付。该程序设置 10 秒 CTest 上限，以捕捉嵌套加锁造成的死锁。

`registration_tests` 覆盖：

- 正常消息解析后的 Worker ID、总资源和初始可用资源
- Worker ID 为 `0` 的合法消息
- `REGISTER_WORKER 1+8+16384` 的解析结果
- 空消息、错误命令、缺失或多余字段
- 负 ID、非正资源、非整数、数字后缀和内存整数溢出

`tcp_io_tests` 覆盖：

- 完整消息接收，返回内容不包含换行
- 空行返回有值的空字符串
- 128 字节正文加换行的合法边界
- 129 字节正文被拒绝
- 发送端在换行前关闭，消息被判定为不完整
- 发送端保持连接但无数据，接收端等待超时后返回空值

收发测试使用 `socketpair(AF_UNIX, SOCK_STREAM, ...)` 创建已连接的本地 socket 对，不占用 TCP 端口。每个用例独立创建和关闭 socket；超时用例使用 100 ms，并允许实际等待耗时有偏差。`tcp_io_tests` 设置了 10 秒的 CTest 执行上限，防止回归导致测试挂住。直接运行时，预期失败场景会打印超长、不完整和超时日志；测试是否通过以用例检查和退出码为准。

`job_submission_tests` 覆盖：

- 正常 Job 的 ID、CPU 和内存字段
- ID 为 0、多余空白、`int` 最大值和相邻带正号数字
- 错误命令、缺失或额外字段、负 ID、非正资源、非整数、数字后缀及三个整数字段的溢出
- 解析后的 Job 无 Worker 时可入队，重复 ID 不改变等待数量
- Worker 与 Job ID 可以相同，入队不分配资源或进入运行状态
- 显式调用调度后，解析得到的多个 Job 保持 FIFO 顺序

`job_completion_tests` 覆盖正常完成消息、0 和 `int` 最大值、正号与空白，以及错误命令、缺失或额外字段、负 ID、非整数、数字后缀和溢出。

`scheduler_protocol_tests` 直接调用服务端使用的 `processSchedulerRequest()`，覆盖五类命令分流、无 Worker/空队列/容量不足、严格 FIFO、提交→调度→完成→资源恢复的状态转换、重复完成、完成等待中或未知 Job，以及非法请求不修改状态。另覆盖领取→完成闭环、领取请求校验、未知 Worker 与客户端使用的调度结果和 `RUN_JOB` 解析函数，包括合法边界和畸形响应。

`tcp_benchmark_tests` 使用临时 TCP 服务端验证压测工具：多个客户端确实同时发出请求、总请求数分配、预热不计入正式样本、分段响应、畸形/超长响应、提前断开、超时、成功与失败混合记账、分位数计算、多轮原始数据保存、失败退出码和预热失败记录。同步屏障要求三个客户端都发来请求后才返回响应，避免依靠固定休眠来判断是否并发。

`epoll_server_integration_tests` 每个用例启动一个全新的真实 C++ 服务端，验证注册/提交/领取/完成、持久连接、分段请求、多行与不完整尾行、半关闭、128 字节边界、超长帧、无效字段、断开清理、慢客户端隔离，以及 12 个客户端同时领取时只派发一次、重复完成不会重复释放资源。各连接使用超时作为失败上限，通过完整响应和 barrier 同步，不使用固定休眠判断结果。

`epoll_server_io_tests` 向通用事件循环注入 8 MiB 响应，验证跨事件的发送偏移、输出积压后的继续解析、响应顺序、半关闭后完整响应、慢读者不阻塞其他连接，以及输出仍待发送时客户端 reset 后继续服务。实际 Scheduler 协议响应较短，大响应只用于覆盖网络层的发送路径。

空闲超时测试向 `EpollServer` 注入可控的单调时钟，验证截止前 1 ms 保持连接、恰好到期关闭、未完成请求不交给 handler、分段接收与正字节发送续期、独立截止时间、同时到期、积压输出清理、禁用配置及非法参数。测试推进逻辑时间，不用固定休眠等待超时。真实服务端集成测试另验证无网络事件时计时唤醒、持久连接响应后及半条请求的超时清理、连接超时不重复释放 Job 资源，以及 32 条完成请求的持久连接到期后服务端 `/proc/<PID>/fd` 数从初始值加 32 回到初始值，再正常处理新请求。CLI 覆盖负数、溢出、非数字、后缀、缺值、重复参数及显式 0；阻塞版拒绝该选项。

空闲超时阶段基线为提交 `d530c92`，改动前后 Release 构建及 11/11 CTest 均通过；新增 fd 回收用例也单独通过。使用下面的既有检查命令，对两个 epoll 测试运行 `-Wall -Wextra -Wpedantic`、AddressSanitizer 和 UndefinedBehaviorSanitizer，均通过且没有编译警告或 sanitizer 报告：

```bash
cmake -S . -B build/epoll-check -DCMAKE_BUILD_TYPE=Debug \
  '-DCMAKE_CXX_FLAGS=-Wall -Wextra -Wpedantic -fsanitize=address,undefined -fno-omit-frame-pointer' \
  '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined'
cmake --build build/epoll-check --target scheduler_server_epoll epoll_server_io_tests --parallel 2
ctest --test-dir build/epoll-check --output-on-failure -R '^epoll_server_'
```

`tcp_comparison_tests` 验证两版 quiet/默认日志模式、参数校验、注册到完成的协议流程、同端口重启、每轮全新进程、交替运行顺序和原始结果保存。目标端口已被占用时，一键工具保存停止原因并退出；测试同时检查原有服务端仍可处理请求。`tcp_benchmark_tests` 还验证接收超时、连接拒绝和错误响应的失败阶段分类。

连接上限测试在 cap=2 时保持两条连接，再让 96 条携带请求的连接排队；跨两轮 accept 拒绝超额连接，同时验证已有连接继续得到响应、拒绝的请求未调用 handler。另覆盖半关闭且积压 8 MiB 输出时仍占名额，完整发送后释放，以及 reset 和可控时钟超时后的名额恢复。真实服务端测试保持两条连接，拒绝 16 次 Job 提交后仍能在已有连接上首次提交同一 ID、领取及完成，并拒绝重复完成；稳定状态下服务端 fd 数为初始值加 2，全部关闭后回到初始值。CLI 覆盖负数、溢出、非数字、后缀、小数、缺值、重复参数、显式 0 和最大合法 int，以及阻塞入口拒绝该选项。测试以响应、EOF/reset 和手动推进事件循环同步，不使用固定休眠。

连接上限阶段基线为提交 `ff9ebba`；改动前后均运行 `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`、`cmake --build build --parallel 2` 和 `ctest --test-dir build --output-on-failure`，11/11 CTest 通过。沿用上面的 sanitizer 命令，两个 epoll 测试 2/2 通过，没有编译警告或 ASan/UBSan 报告；集成测试检查 quiet 模式下子进程 stdout/stderr 为空。本轮验证连接数量约束、拒绝行为和资源恢复，没有新增性能测量。

backlog 实验新增了正值传递、0/负数/溢出/缺失/重复参数校验，以及内核计数解析、采样不可用和计数重置的处理。C++ I/O 测试在事件循环尚未运行时，使用 backlog=128 建立 8 个等待 accept 的连接，验证监听队列配置实际生效；Python 测试验证阻塞版被第一个空闲连接占住时，较大队列仍可容纳后续突发连接。这些测试验证排队容量，不把排队等同于请求处理并行。

慢连接实验另覆盖：目标进程确实 accept 后才开始正常流量、已握手但尚未 accept 的连接不会被误判、定时补字节、补齐换行后的正确响应、正常请求失败阶段和恢复探测保存。实验结束校验失败时，工具保留原始请求样本、记录停止原因并取消汇总，避免把无效场景当作性能结果。

当前 CTest 会启动 C++ epoll 服务端验证网络集成；阻塞版的 5 秒超时配置和原 C++ 客户端进程的全部响应分支仍需手动验证。阻塞式 `sendAll()` 的部分发送分支及系统调用 `EINTR` 分支尚未由测试专门触发。

## Benchmark

使用 Release 构建后运行：

```bash
./build/scheduler_benchmark
```

Benchmark 测量单进程调度性能，不包含 TCP 通信。它使用固定随机种子生成 100 个 Job，并让三种策略处理完全相同的输入。每种策略重复运行 10,000 次，每次都创建全新的 Scheduler 和 Worker。

计时只覆盖 `scheduleNext()` 调用（包括待派发 ID 入队），不包含 Scheduler 创建、Job 提交、快照复制、指标计算或输出。报告的时间是每次调度尝试的平均纳秒耗时；一次实验的尝试次数包含成功调度调用以及最后一次用于结束 FIFO 循环的失败调用。

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

### TCP 服务 benchmark

`benchmarks/tcp_benchmark.py` 测量完整的短连接请求，与上面的调度函数 benchmark 分开。单独测试时，先手动启动全新的服务端并添加 `--quiet`。两版自动对比使用下文的 `compare_tcp_servers.py`，它管理自己创建的服务端子进程并固定测量条件。

使用没有 Worker/Job、且没有其他客户端访问的全新服务端。每个连接只发送 `SCHEDULE_NEXT\n`（14 字节），预期 `SCHEDULE_NONE\n`（14 字节）。错误响应、EOF、连接失败、超时均计为失败，不自动重试。该负载衡量空队列的网络服务路径，不代表任务执行或复杂调度性能。

```bash
python3 benchmarks/tcp_benchmark.py \
  --requests 1000 --warmup 10 --concurrency 1 10 50 100 \
  --repeat 3 --timeout 1 --label blocking \
  --server-binary build/scheduler_server \
  --notes 'Release; fresh empty server started with --quiet' \
  --output build/blocking-results.json
```

可以用 `--port` 指定端口，用 `--server-pid <PID>` 在 Linux 上采样该进程的 CPU 和内存。压测程序不启动、重置或停止目标服务端；不要同时运行 Worker 或 Job 客户端。服务端标准输出和标准错误应重定向到文件，避免终端输出影响结果，两版必须采用相同方式。

测量口径：

- 每个并发数、每一轮均先串行预热 `--warmup` 次，预热失败会停止后续测量，已完成轮次和停止原因仍写入 JSON。
- 固定数量的 Python 客户端线程通过 barrier 一起开始。每个线程串行处理分配到的请求份额，所有线程合计恰好 `--requests` 次；不是每个线程发送这么多次。并发数不能超过总请求数。
- 这是闭环短连接负载：每个客户端等当前请求结束后才发下一个请求，最多有指定数量的请求同时进行。各线程份额相差最多一次，测试尾部实际并发会降低。
- 成功请求延迟从开始建立连接到收到完整响应，包含连接等待、服务端排队及处理、传输和 Python/操作系统调度，不含响应校验与关闭连接。`--timeout` 限制单次 socket 操作，不是整条请求的总截止时间。
- 正式总耗时从 barrier 释放到全部请求结束，包含失败等待和客户端记账，不含预热、线程创建、指标汇总及输出。成功吞吐 = 成功数 / 正式总耗时。
- P50/P95 只使用成功请求的延迟，按 nearest-rank 定义取排序后第 `ceil(p × N)` 个样本。必须同时阅读失败率；大量超时可以使成功请求的 P95 看起来仍很低。
- 跨轮汇总逐项取中位数，不把所有轮的延迟混在一起。某轮无成功样本，则该并发数汇总的延迟显示 `null`，不会悄悄省略失败轮。
- JSON 保留每轮成功耗时（ns）、每个失败请求的耗时及原因、吞吐和分位数、环境、源码提交、脚本/可选服务端二进制 SHA-256、命令和备注。新数据为失败请求增加 `phase`：`connect`（建立连接）、`send`（发送请求）、`receive`（等待/读取响应）、`validate`（响应不符合预期）。阶段分类基于客户端操作，不代表已经定位内核内部原因。任意正式请求失败时退出码为 1，结果仍会保存。
- 可选 CPU 指标从 `/proc/<PID>/stat` 的 CPU 时间增量计算，100% 表示占满一个逻辑核，精度受内核时钟 tick 影响。`VmHWM` 是服务端进程生命周期的 RSS 峰值，不是本轮独立峰值；无法采样时明确记录 unavailable。客户端 CPU 和内存未测量。

#### 重建已保存的阻塞基线

历史阻塞版代码固定在提交 `2f30211b75f0fc65aa40ab7843957363f94877ef`。当时端口 9000 被占用，因此已保存测量只把该服务端副本的监听端口和启动日志改为 19000，原有服务端没有被停止或修改。以下命令在独立 worktree 中重建；端口变更不影响连接处理逻辑，未来比较时两版应使用相同端口和配置。

```bash
git worktree add --detach build/blocking-source 2f30211b75f0fc65aa40ab7843957363f94877ef
python3 - <<'PY'
from pathlib import Path
path = Path('build/blocking-source/apps/scheduler_server.cpp')
source = path.read_text()
assert source.count('htons(9000)') == 1
path.write_text(source.replace('htons(9000)', 'htons(19000)')
               .replace('127.0.0.1:9000', '127.0.0.1:19000'))
PY
cmake -S build/blocking-source -B build/blocking-build -DCMAKE_BUILD_TYPE=Release
cmake --build build/blocking-build --target scheduler_server --parallel 2
```

终端一启动基线（需要空闲的 19000 端口），看到日志中的 `Listening` 后继续：

```bash
./build/blocking-build/scheduler_server > build/blocking-server.log 2>&1
```

终端二执行；若要 CPU/RSS，请额外传入上面启动的服务端 PID：

```bash
python3 benchmarks/tcp_benchmark.py --port 19000 \
  --requests 1000 --warmup 10 --concurrency 1 10 50 100 \
  --repeat 3 --timeout 1 --label blocking \
  --server-binary build/blocking-build/scheduler_server \
  --notes '2f30211; Release -O3 -DNDEBUG; port-only change; logs redirected to file' \
  --output build/blocking-reproduced.json
```

测试后在终端一按 Ctrl+C。如果要移除临时 worktree，先撤销该副本的端口修改，再执行 `git worktree remove build/blocking-source`；不要对主工作区执行恢复操作。

#### 已测阻塞版结果

原始数据保存在 [`benchmarks/results/blocking_baseline.json`](benchmarks/results/blocking_baseline.json)，包含全部 12 轮及逐请求数据。环境为 WSL2 / Linux 6.6.87.2、AMD Ryzen 7 6800H（16 个逻辑 CPU）、GCC 13.3.0，Release `-O3 -DNDEBUG`。每轮 1000 次正式请求、10 次串行预热、单次 socket 操作超时 1 秒，每档重复 3 轮。所有轮次共 12,000 次正式请求，成功 9,935 次，超时失败 2,065 次；无中途停止。构建和原有测试通过后测量，新增压测测试也已通过。

| 并发客户端 | 成功吞吐中位数（请求/秒） | 成功 P50 中位数（ms） | 成功 P95 中位数（ms） | 单轮失败率中位数 | 三轮失败总数 | 服务端 CPU 中位数（单核 %） |
|---:|---:|---:|---:|---:|---:|---:|
| 1 | 4694.01 | 0.180 | 0.233 | 0.0% | 0 / 3000 | 70.18 |
| 10 | 242.78 | 0.220 | 0.425 | 2.0% | 57 / 3000 | 2.97 |
| 50 | 81.20 | 0.321 | 5.828 | 26.4% | 773 / 3000 | 1.10 |
| 100 | 85.13 | 0.361 | 11.287 | 39.7% | 1235 / 3000 | 0.99 |

服务端进程生命周期 RSS 峰值为 3200 KiB；各轮共用同一个空状态服务端，因此这不是各轮独立测得的内存峰值。CPU 来自内核 tick 增量，短时测量存在量化误差。服务端日志重定向到文件，客户端和服务端在同一台机器上，没有控制系统后台负载。

高并发下失败增加且 CPU 较低，与连接排队/等待的现象一致。当前服务端串行处理请求且 `listen()` backlog 为 1，测试又使用短连接和 1 秒超时；尚未用控制变量实验或内核计数器区分这些因素的贡献，不能把吞吐下降全部归因于阻塞 I/O，也不能把这些结果当作服务端的最大处理能力。Python 线程、GIL、操作系统调度与 TCP 连接重传同样在端到端测量边界内。后续先保持 backlog 等条件一致比较 epoll，再单独研究 backlog 调优，避免把两种优化混为一个收益。

以上是旧版带请求日志的历史测量，不能直接与现在的 quiet 模式相减得出 epoll 收益。条件统一后的对比另存于下文，保留这份历史数据用于追溯。空队列压测不验证真实调度负载或 Worker 执行；慢客户端隔离和资源记账由上面的 epoll 集成测试独立验证。

#### 一键比较两版服务端

本轮假设：统一请求日志条件后，epoll 可以交错推进多个连接的 I/O；短连接吞吐仍可能受 backlog=1、连接建立及 Python 驱动影响。比较对象是当前两套服务端实现，包含逐字节/批量接收、accept 与事件管理、连接关闭方式等差异，不能把结果解释为仅仅一个 epoll 系统调用带来的收益。

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
python3 benchmarks/compare_tcp_servers.py --build-dir build \
  --requests 1000 --warmup 10 --concurrency 1 10 50 100 \
  --repeat 3 --timeout 1 --output-dir build/quiet-comparison
```

工具要求 Release 构建，每版共 12 轮、24 轮合计 24,000 次正式请求。首次自动选择空闲端口，此后两版都重用该端口；也可用 `--port` 指定端口。每轮启动全新空状态服务端、先做串行预热，再调用同一个压测函数。第 1、3 次重复先 blocking 后 epoll，第 2 次反过来；测量时仅有一个目标服务端运行。两版均为 backlog=1、`SO_REUSEADDR`、`--quiet`，成功/失败请求期间均无请求日志；工具检查启动后 stdout 和 stderr 确实为空。测试窗口相邻且交替执行，但 3 次重复无法完全消除系统负载和顺序偏差。

对比工具支持 `--backlog <正 int>`，默认 1；该值同时传给两种服务端，写入配置和进程命令。数据另记录 `somaxconn` 等内核设置，以及 `/proc/net/netstat` 中 `ListenOverflows/ListenDrops` 的前值、后值和增量。历史数据的窗口为预热开始前到正式请求结束后；加入慢连接实验后的新窗口还包含连接设置、释放及恢复探测，具体见每轮 `listen_counters.window`。其范围是整个网络命名空间，无法与单个服务端 PID 严格对应；读不到或计数倒退时明确记录 unavailable，不填造 0。stderr 文件名包含慢连接数和 backlog，避免不同组实验互相覆盖日志。

输出目录包含 `blocking.json`、`epoll.json` 和 `comparison.json`。前两份保存全部原始成功/失败样本和各轮 CPU/RSS，第三份记录配对顺序、端口、进程命令、源码/脚本/二进制哈希、CMake 构建选项和对比汇总。每轮都使用独立进程，因此内存指标是每轮进程从启动开始、包含预热的生命周期峰值；CPU 仍只覆盖正式测量采样窗口。stderr 日志位于 `build/tcp-comparison-logs/`。工具会停止自己启动的进程，即使请求失败也保存结果；存在请求失败时返回 1，启动或预热失败时另记录 `aborted`。

本轮改动前提交为 `59cfcb5e30fd5991635f49db486b34c563903db2`。为统一条件，增加了共享端口/quiet 参数、共享默认请求日志和两版的地址复用配置；原阻塞 I/O、5 秒接收等待超时和 epoll 的事件处理预算保持原逻辑。新的分阶段失败记录应用于两版客户端测量，不与旧数据混用。

#### quiet 模式下的前后对比结果

原始数据保存于 [`benchmarks/results/quiet_comparison/`](benchmarks/results/quiet_comparison/)。实际执行上面的一键命令，仅将 `--output-dir` 改为该路径。机器仍为 AMD Ryzen 7 6800H、16 个逻辑 CPU、WSL2 / Linux 6.6.87.2、GCC 13.3.0，Release `-O3 -DNDEBUG`；首次分配并在全部 24 轮中复用端口 43699。构建和 11/11 CTest 通过后测量，两版所有轮次的 quiet 输出检查通过。后台系统负载没有隔离。

下表的吞吐是三轮中位数；失败数是三轮合计，每个单元格分母均为 3000 次正式请求。

| 并发客户端 | blocking 成功请求/秒 | epoll 成功请求/秒 | blocking 失败数 | epoll 失败数 |
|---:|---:|---:|---:|---:|
| 1 | 5093.66 | 4900.20 | 0 / 3000 | 0 / 3000 |
| 10 | 243.70 | 2074.46 | 48 / 3000 | 0 / 3000 |
| 50 | 74.60 | 772.02 | 779 / 3000 | 4 / 3000 |
| 100 | 67.79 | 2109.42 | 1291 / 3000 | 1 / 3000 |

成功请求的分位数也是逐轮计算后取三轮中位数，单位 ms：

| 并发客户端 | blocking P50 | epoll P50 | blocking P95 | epoll P95 |
|---:|---:|---:|---:|---:|
| 1 | 0.153 | 0.152 | 0.229 | 0.225 |
| 10 | 0.202 | 4.328 | 0.436 | 6.310 |
| 50 | 0.301 | 19.947 | 5.952 | 30.800 |
| 100 | 0.367 | 41.309 | 12.633 | 66.221 |

两版各完成 12,000 次正式请求：blocking 成功 9882 次，失败 2118 次（2117 次连接阶段超时，1 次接收阶段超时）；epoll 成功 11995 次，失败 5 次，均为连接阶段超时。无测量中途停止，工具退出码为 1 表示仍有失败请求，完整结果已保存。单轮失败率的三轮中位数，blocking 为 0%、1.7%、25.7%、45.2%，epoll 为 0%、0%、0.1%、0%；epoll 在并发 100 的失败率中位数为 0%，但三轮合计仍有 1 次失败，不能描述成该档没有失败。

服务端 CPU（100% 为一个逻辑核）和每轮进程生命周期 RSS 峰值的三轮中位数如下。CPU 由内核 tick 采样，RSS 包含启动和预热；客户端资源未测量。

| 并发客户端 | blocking CPU % | epoll CPU % | blocking RSS KiB | epoll RSS KiB |
|---:|---:|---:|---:|---:|
| 1 | 40.71 | 53.85 | 3200 | 3200 |
| 10 | 1.74 | 18.66 | 3200 | 3200 |
| 50 | 0.55 | 6.96 | 3200 | 3200 |
| 100 | 0.49 | 17.11 | 3200 | 3200 |

本机这组短连接实验中，epoll 在单客户端下的成功吞吐略低；并发 10/50/100 时成功吞吐更高、失败更少，但成功请求的 P50/P95 更高。两版成功样本集合不同，阻塞版大量超时请求被排除在成功延迟统计之外，所以不能据此宣称 epoll 改善所有请求的延迟，也不能仅凭成功 P95 判断哪版整体体验更好。CPU 百分比的采样窗口长短与完成工作量不同，低 CPU 不等于更高效率。

吞吐也没有随并发数单调变化。epoll 并发 50 的三轮成功吞吐分别为 780.59、725.30、772.02 请求/秒，每轮有 1～2 次连接超时；并发 100 的三轮为 2109.42、2139.90、710.15 请求/秒，第三轮有 1 次连接超时。固定份额的闭环客户端中，即使仅少数请求等待 1 秒超时，也可能成为整轮最后完成的线程，明显拉长总测量时间。这些波动保留在原始数据中，未删除或补跑挑选更好的轮次。

这一历史实验的分阶段记录把失败集中定位在客户端的连接操作，但当时没有抓包、内核队列计数或 CPU profiling，不能确定连接阶段内部各因素的贡献，也不能把成功延迟的增加归因于某个已证实的单一原因。下文独立改变 backlog 并增加监听队列计数采样；这些数据仍不证明生产规模、复杂调度或真实 AI 工作负载下的收益。

#### backlog 控制变量实验

Linux 的 TCP backlog 控制已完成连接建立、等待应用 `accept()` 的队列长度，超过 `somaxconn` 的请求值会被内核截断。它与未完成握手队列的 `tcp_max_syn_backlog` 不是同一个配置。增大 backlog 提供更多等待位置，应用仍需要及时 accept 和处理请求。[listen(2)](https://man7.org/linux/man-pages/man2/listen.2.html)

本轮假设：将 backlog 从 1 提高到 128，在相同短连接负载下能减少连接阶段超时及监听队列溢出，尤其是串行接受连接的阻塞版。两版的线程模型、帧解析、读写预算、日志和请求内容均保持原逻辑。默认仍为 1，以保留原基线；通过显式参数做独立实验。

在 Release 构建和全部 CTest 通过后，依次执行以下两条命令。需要空闲的 19000 端口；每组结束后工具会停止其创建的服务端。

```bash
python3 benchmarks/compare_tcp_servers.py --build-dir build --port 19000 \
  --backlog 1 --requests 1000 --warmup 10 --concurrency 1 10 50 100 \
  --repeat 3 --timeout 1 --output-dir build/backlog-experiment/backlog_1

python3 benchmarks/compare_tcp_servers.py --build-dir build --port 19000 \
  --backlog 128 --requests 1000 --warmup 10 --concurrency 1 10 50 100 \
  --repeat 3 --timeout 1 --output-dir build/backlog-experiment/backlog_128
```

两组共 48 轮、48,000 次正式请求；每组内部仍交替测试两版，每轮使用新进程。两组使用同一端口、同一份二进制和相同驱动；先完整测 backlog=1 再测 128，因此组间时间顺序和未隔离的后台负载仍是限制。比较使用本轮新测的 backlog=1，历史结果保留供追溯。存在请求失败时工具返回 1 并保存完整数据。

`ListenOverflows` 与监听队列满有关；`ListenDrops` 还可能包含其他监听路径丢弃。它们计数的是内核事件，重传和其他端口的流量也可能影响增量，不能把增量直接当作失败请求数。定义参考 [Linux 内核 SNMP 计数文档](https://docs.kernel.org/networking/snmp_counter.html)。本轮仅增加只读采样，不更改系统网络设置。

原始数据保存在 [`benchmarks/results/backlog_experiment/`](benchmarks/results/backlog_experiment/)，实际命令仅把上面两个输出目录改为该路径下的 `backlog_1` 和 `backlog_128`。改动前提交为 `e27714d`；改动前后均通过 11/11 CTest，正式测量在改动后的测试通过后进行。环境仍是 WSL2 / Linux 6.6.87.2、Ryzen 7 6800H、GCC 13.3.0、Release `-O3 -DNDEBUG`。两组二进制及源码/驱动哈希一致，48 个服务端 PID 均不同，全部使用端口 19000。记录的内核设置为 `somaxconn=4096`、`tcp_max_syn_backlog=512`、`tcp_syncookies=1`、`tcp_abort_on_overflow=0`。

成功吞吐取三轮中位数，失败数为每档三轮合计（每格分母 3000 次请求）：

| 服务端 | 并发 | backlog=1 请求/秒 | backlog=128 请求/秒 | backlog=1 失败数 | backlog=128 失败数 |
|---|---:|---:|---:|---:|---:|
| blocking | 1 | 4564.40 | 5078.70 | 0 | 0 |
| epoll | 1 | 4136.27 | 4776.92 | 0 | 0 |
| blocking | 10 | 243.19 | 2030.91 | 49 | 0 |
| epoll | 10 | 721.33 | 2007.74 | 5 | 0 |
| blocking | 50 | 83.88 | 2031.40 | 745 | 0 |
| epoll | 50 | 869.73 | 1985.79 | 1 | 0 |
| blocking | 100 | 69.83 | 1935.63 | 1307 | 0 |
| epoll | 100 | 847.67 | 1941.01 | 4 | 0 |

backlog=1 时，blocking 总共 9899 次成功、2101 次连接超时，epoll 为 11990 次成功、10 次连接超时；backlog=128 时两版各 12,000 次成功、0 失败。两组均无中途停止，退出码分别为 1 和 0。新一轮 backlog=1 不完全复现历史轮次的数值，这是保留相邻实验基线的原因。

backlog=1 的 blocking 采样窗口累计 `ListenOverflows=2287`、`ListenDrops=2287`，epoll 为 15、15；backlog=128 的全部采样窗口这两项增量均为 0。计数按网络命名空间采样，包含预热且可能受其他流量影响；大于客户端失败数不表示重复请求失败，二者不是相同计数对象。结合相同二进制的参数对照、连接阶段超时和溢出增量变化，这组数据支持：小监听队列是本机这组突发短连接实验的重要限制，不能把 backlog=1 下的大吞吐差距全部归因于 epoll。

backlog=128 时，两版成功请求的延迟、CPU 三轮中位数如下。延迟单位为 ms，CPU 的 100% 表示一个逻辑核；每轮进程生命周期 RSS 峰值中位数均为 3200 KiB。

| 并发 | blocking P50 | epoll P50 | blocking P95 | epoll P95 | blocking CPU % | epoll CPU % |
|---:|---:|---:|---:|---:|---:|---:|
| 1 | 0.152 | 0.156 | 0.233 | 0.222 | 40.59 | 52.50 |
| 10 | 4.516 | 4.518 | 6.388 | 6.560 | 12.20 | 19.86 |
| 50 | 22.130 | 21.951 | 31.808 | 36.003 | 14.21 | 20.11 |
| 100 | 45.768 | 42.874 | 68.635 | 73.314 | 13.54 | 19.40 |

队列增大后，并发 10～100 的两版吞吐接近，未观察到 epoll 在该正常短请求场景中的普遍吞吐优势。阻塞版成功 P95 比 backlog=1 时更高，但成功样本从大量失败后的子集变为全部请求，不能只比较条件分位数而忽略完成率。并发 1 的溢出计数两组均为 0，吞吐仍有变化，也说明组间时间顺序、客户端与系统调度等因素存在，不能把每个数值差异都算作 backlog 的收益。尚未采样客户端 CPU 或做 profiling，不判断约 2000 请求/秒的具体限制来自哪里。

backlog 增大只改善排队容量；阻塞版仍会在一个客户端发不完请求时停在接收循环。下节固定 backlog=128，使用一条不完整请求测量其他客户端的完成率和延迟，进一步区分排队容量与连接处理方式的作用。

#### 慢连接与正常请求混合实验

本轮假设：一条已被接受但不发送换行符的请求，会让阻塞版停在 `receiveLine()`；epoll 能保存其输入状态并继续处理其他连接。主要指标是正常请求完成率及成功请求 P50/P95，成功吞吐仍包含失败等待。服务端沿用提交 `d78e099` 的二进制与 C++ 源码，本轮仅扩展 Python 工具、测试和文档。

`compare_tcp_servers.py --slow-clients 1` 使用 `slow_client.py` 管理一条慢连接；默认 `--slow-clients 0` 为正常负载。步骤如下：

1. 启动新服务端并完成串行预热。
2. 慢连接发送 `SCHEDULE_NEXT `，没有换行。工具匹配 `/proc/<PID>/net/tcp` 中的连接四元组和 inode，并确认该 socket 已出现在目标进程的 `/proc/<PID>/fd`，避免把仅握手成功误认为 accept 完成。无法确认时停止实验。
3. 后台线程每 0.5 秒追加一个空格，不完成帧。这样避免阻塞版的单次 5 秒接收超时提前结束场景；这不是固定休眠后猜测服务端状态。随后按原有 barrier 启动正常请求，测量边界与普通 TCP benchmark 相同。
4. 正常请求结束后，停止补字节，检查慢连接尚无响应或 EOF，再补换行、半关闭写方向并验证 `SCHEDULE_NONE\n`。最后用新连接探测服务端恢复。两组均执行恢复探测，超时为 1 秒。

预热、慢连接建立/释放、恢复探测均不计入正式吞吐、延迟和 CPU 窗口。慢连接本身不计作正式请求；这是 **1 条慢连接 + 最多 10/50/100 条正常连接**，每轮正常请求总数为 100，不表示 1% 的请求混合比例。工具限定慢连接模式 backlog=128、正常请求数小于 128，并要求 `ceil(requests / min(concurrency)) × timeout <= 30 秒`，避免用长实验触及 128 字节协议限制；结束校验仍是场景有效性的必要条件。

改动前后 Release 构建及 11/11 CTest 均通过后，依次运行：

```bash
python3 benchmarks/compare_tcp_servers.py --build-dir build --port 19000 \
  --backlog 128 --slow-clients 0 --requests 100 --warmup 10 \
  --concurrency 10 50 100 --repeat 3 --timeout 0.2 \
  --output-dir benchmarks/results/slow_client_experiment/control

python3 benchmarks/compare_tcp_servers.py --build-dir build --port 19000 \
  --backlog 128 --slow-clients 1 --requests 100 --warmup 10 \
  --concurrency 10 50 100 --repeat 3 --timeout 0.2 \
  --output-dir benchmarks/results/slow_client_experiment/one_slow
```

原始结果见 [`benchmarks/results/slow_client_experiment/`](benchmarks/results/slow_client_experiment/)。两组共 36 轮、3600 次正常请求，使用 36 个不同服务端 PID、同一端口 19000；每组内部仍交替两版顺序。环境为 WSL2 / Linux 6.6.87.2、Ryzen 7 6800H（16 逻辑核）、Python 3.12.3、GCC 13.3.0、Release `-O3 -DNDEBUG`。两组 C++ 源码、各自二进制、压测驱动、对比工具及慢连接辅助模块哈希均已核对一致。新增辅助模块哈希位于 `comparison.json`。

成功吞吐与延迟为三轮中位数，延迟单位 ms。无慢连接时，每版每档 300/300 正常请求成功；有慢连接时，blocking 每档 0/300 成功、300 次接收超时，epoll 每档 300/300 成功、0 失败。

| 正常并发 | 无慢连接 blocking 请求/秒 | 无慢连接 epoll 请求/秒 | 无慢连接 blocking P50 / P95 | 无慢连接 epoll P50 / P95 | 有慢连接 epoll 请求/秒 | 有慢连接 epoll P50 / P95 |
|---:|---:|---:|---:|---:|---:|---:|
| 10 | 2070.36 | 2029.72 | 4.263 / 6.196 | 4.288 / 6.163 | 2083.41 | 4.170 / 6.101 |
| 50 | 1773.35 | 1802.47 | 21.250 / 30.990 | 20.747 / 35.972 | 1872.00 | 20.142 / 31.268 |
| 100 | 1713.80 | 1767.17 | 42.300 / 51.952 | 37.815 / 49.865 | 1851.47 | 37.954 / 47.763 |

有慢连接时 blocking 各档成功吞吐均为 0，成功 P50/P95 均不可用（JSON `null`），不能填写 0 ms。control 退出码为 0；one_slow 因 900 次正常请求失败返回 1，全部结果仍保存，两组 `aborted` 均为空。18 条慢连接全部确认 accepted，正文长度最终为 14～18 字节、未提前响应或断开，补齐后全部返回预期响应；36 次恢复探测全部成功。所有窗口的 `ListenOverflows/ListenDrops` 增量为 0；结合接受确认与失败阶段，观察结果支持阻塞版等待不完整帧导致后续正常请求无法及时处理。

服务端 CPU 三轮中位数如下，100% 表示一个逻辑核；两组全部进程生命周期 RSS 峰值为 3200 KiB。窗口很短，CPU 时间受内核 tick 精度限制，采样的 0% 不表示没有执行代码，不据此计算 CPU 效率提升。

| 正常并发 | control blocking CPU % | control epoll CPU % | one_slow blocking CPU % | one_slow epoll CPU % |
|---:|---:|---:|---:|---:|
| 10 | 0.00 | 20.21 | 0.00 | 0.00 |
| 50 | 0.00 | 17.95 | 0.00 | 16.43 |
| 100 | 0.00 | 17.60 | 0.00 | 18.44 |

这是短时间、单条慢连接、空队列和本机 loopback 的受控实验；先测 control 再测 one_slow，后台负载未隔离，客户端 CPU 未测量。epoll 两组数值的小幅变化不能解释为慢连接带来性能提升，也不能证明所有负载下 epoll 更快。该实验验证不完整请求下的服务隔离，不覆盖大量慢连接、长期运行、连接配额、空闲超时或真实 Worker 执行。请求失败不等于未被服务端执行，释放慢连接后的排队请求可能继续被处理；因此选择无状态的空队列请求，恢复阶段不计入性能窗口。

#### epoll 第一版验证记录

改动前提交为 `c14f6da3f23143381ecca94c59b3b33e970662c7`，使用 GCC 13.3.0、CMake 3.28.3、WSL2 / Linux 6.6.87.2。以下命令在 Release 下改动前通过 8/8 CTest，改动后通过 10/10 CTest：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

额外使用警告、AddressSanitizer 与 UndefinedBehaviorSanitizer 检查两个 epoll 测试，2/2 通过，没有出现编译警告或 sanitizer 报告：

```bash
cmake -S . -B build/epoll-check -DCMAKE_BUILD_TYPE=Debug \
  '-DCMAKE_CXX_FLAGS=-Wall -Wextra -Wpedantic -fsanitize=address,undefined -fno-omit-frame-pointer' \
  '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined'
cmake --build build/epoll-check --target scheduler_server_epoll epoll_server_io_tests --parallel 2
ctest --test-dir build/epoll-check --output-on-failure -R '^epoll_server_'
```

已有 benchmark 对新入口做单客户端功能检查，100 次正式请求全部成功，结果位于本地 `build/epoll-smoke.json`；这不是与阻塞版条件一致的性能实验，不用其耗时计算提升。

## 当前限制

- 阻塞版串行处理连接，单次接收等待最多约 5 秒，没有整个请求的截止时间；持续缓慢发送字节可能长时间阻塞后续处理
- epoll 空闲清理和连接上限默认关闭；空闲检查每轮 O(N) 扫描，持续补字节的慢连接仍可续期，尚无整个请求的总截止时间或优雅停机，也未处理 fd 耗尽后的恢复；超额连接直接关闭，没有业务错误响应或自动重试
- epoll 的每轮预算与输出高水位只限制网络处理，协议处理仍在事件循环中同步执行；大规模调度计算也可能影响其他客户端延迟
- Worker 状态、等待 Job 与运行记录仅保存在内存中，服务端退出后消失
- Worker 退出不会注销；待派发任务和运行记录可能长期保留，资源不会自动回收
- 两版服务端均可配置端口、固定绑定 loopback；原 C++ 客户端地址/端口仍固定
- 网络 Job 通过 Worker 轮询触发调度；空闲轮询间隔为 1 秒，领取时机还受调度策略、执行和资源状态影响，尚无服务端主动推送
- 状态修改与响应发送不是原子操作，尚无请求结果缓存或自动恢复机制
- Scheduler 使用一把粗粒度 mutex，尚未研究锁竞争
- Job 只是模拟的资源请求，不执行真实工作负载
- Worker 进程串行模拟执行，尚无本地并行执行器；调度器的 running 计数包含已预留但未领取的任务
- 完成上报只包含 Job ID，没有身份认证、Worker 归属校验和执行结果数据
- 没有持久化和已完成 Job 历史
- 没有心跳、故障检测和 Job 重试
- 没有 GPU、GPU 显存或 NPU 资源模型
- Benchmark 使用小规模合成负载，不代表生产环境性能

## 后续计划

- 完善注册失败原因反馈和可配置的连接地址
- 扩展多个 Worker 的并发连接管理与连接生命周期处理
- 接入真实任务执行与执行结果数据
- 添加 Worker 心跳与故障检测
- 添加 Job 失败重试和重新调度
- 扩展 GPU、GPU 显存和 NPU 资源约束
- 增加更完整的调度实验和指标
