# HeteroSched

HeteroSched 是一个使用 C++17 逐步构建的轻量级资源调度器学习项目。已完成单进程资源调度，并通过阻塞式 TCP 提供 Worker 注册、Job 提交、手动触发调度及模拟完成接口。Scheduler 服务端持续监听，串行处理请求，维护队列、运行记录和 Worker 资源，并向客户端反馈结果。

当前进度：Phase 1 单进程调度已完成；Phase 2 已实现基础 TCP 通信、多个 Worker 串行注册、接收超时，以及提交、调度、完成的手动网络闭环。提交成功表示入队，调度成功表示资源已分配并加入运行记录，完成接口模拟任务结束并释放资源。Worker 尚未接收或执行任务；实际网络 Job 分发、执行结果回报、心跳和 GPU 调度属于后续计划。

## 目录结构

公共头文件、实现和测试按同一组职责分类；可执行程序的入口统一放在 `apps/`。

```text
HeteroSched/
├── apps/                       # 五个可执行程序入口
│   ├── demo_main.cpp            # 单进程演示，对应 heterosched
│   ├── scheduler_server.cpp
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
│   └── scheduler_benchmark.cpp
├── CMakeLists.txt
└── README.md
```

头文件引用包含模块路径，例如 `#include "core/job.h"`、`#include "net/tcp_io.h"` 和 `#include "protocol/scheduler_protocol.h"`。`Job` 是纯数据结构，直接定义在 `include/core/job.h` 中，不需要空的实现文件。

CMake 构建三个可复用库：`heterosched_core` 管理领域状态与调度，`heterosched_net` 提供 socket 收发，`heterosched_protocol` 依赖 core 并负责协议处理。`apps/` 按需链接这些库；单进程演示和 benchmark 只依赖 core，Worker/Job 客户端只依赖 net。可执行文件名和下面的运行命令保持一致。

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

### TCP 请求处理

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
            +-- COMPLETE_JOB    -> parseJobCompletion()-> completeJob()
            +-- 未知命令        -> REQUEST_ERROR UNKNOWN_COMMAND
        sendAll() 发送处理结果
        |
    close(clientFd) -> 回到 accept()
```

三类客户端每次启动都只发送一条请求。`main()` 拥有监听 socket 和 Scheduler，负责循环接受连接、配置接收超时及关闭客户端 socket。`handleClient()` 只接收一次完整行，再调用 `processSchedulerRequest()`。协议处理函数位于 `src/protocol/scheduler_protocol.cpp`，按精确命令名分流，校验后调用 Scheduler 并返回带换行的响应；网络入口负责发送结果。请求错误只结束该连接，服务端继续接受下一个连接。

Scheduler 创建在 `accept()` 循环外，四类请求共享同一个服务端的内存状态。网络服务端与单进程调度演示使用各自独立的 Scheduler 实例。`submitJob()` 只负责入队和 Job ID 去重；`SCHEDULE_NEXT` 显式触发 `scheduleNext()` 扣减资源；`COMPLETE_JOB` 显式触发 `completeJob()` 回收资源。Worker 客户端仍只执行注册，不接收或执行 Job。

服务端对每条完整请求输出 `Received`、生成的 `Response`、等待/运行数量以及各 Worker 的可用 CPU 和内存。状态在响应发送之前修改和打印；这些日志不证明客户端已经收到响应。

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
Response: REGISTER_OK
State: workers=1, waiting=0, running=0
Worker 1: available CPU=8, memoryMB=16384
```

客户端完成一次注册尝试后退出；服务端关闭该客户端连接后，继续等待下一次连接。`Registration message sent` 只表示消息已提交给本地 socket；服务端注册成功后回复 `REGISTER_OK`，客户端收到确认后打印 `Registration success`。注册失败时，客户端展示服务端返回的错误码并以非零状态退出。

### 验证连续注册与错误请求

启动一次服务端后，在另一个终端依次执行以下命令，中间不重启服务端：

```bash
./build/worker_client 1 8 16384
./build/worker_client -1 4 8192
./build/worker_client 1 4 8192
./build/worker_client 2 4 8192
```

以上序列从刚启动、尚未注册任何 Worker 的服务端开始；如果已经运行了前面的 Worker 1 演示，第一条请求会被视为重复注册。

| 请求 | 客户端结果 | 服务端 Worker 数量 |
|---|---|---|
| 首次注册 ID 1 | `Registration success` | 1 |
| 负 ID | `Registration failed: INVALID_MESSAGE` | 1 |
| 再次注册 ID 1 | `Registration failed: WORKER_REJECTED` | 1 |
| 注册 ID 2 | `Registration success` | 2 |

服务端成功注册 Worker 2 时输出 `State: workers=2, waiting=0, running=0`。非法输入和重复注册不会终止服务端，也不会增加 Worker 数量。实验结束后，在服务端终端按 `Ctrl+C` 停止程序；下次启动时状态重新为空。

### 运行网络 Job 提交演示

在一个新启动的服务端上，先提交任务，再注册 Worker：

```bash
./build/job_client 101 2 4096
./build/worker_client 101 8 16384
./build/job_client 102 1 2048
```

Job ID 与 Worker ID 可以相同。Job 客户端的三个参数依次为 Job ID、CPU 核数和内存 MB，仍只检查参数数量，参数内容由服务端解析和校验。即使尚未注册任何 Worker，合法 Job 也能进入等待队列。客户端成功时输出：

```text
Connected to Scheduler at 127.0.0.1:9000
Submit message sent
Submit success
```

服务端两次提交对应的状态日志为：

```text
State: workers=0, waiting=1, running=0
State: workers=1, waiting=2, running=0
```

`Submit success` 表示入队成功，不表示已开始运行或执行完成。继续验证错误输入：

```bash
./build/job_client 101 2 4096
./build/job_client -1 2 4096
./build/job_client 103 0 4096
./build/job_client 103 abc 4096
```

重复 ID 返回 `Submit failed: JOB_REJECTED`，后三条非法参数请求返回 `Submit failed: INVALID_MESSAGE`；等待任务数仍为 2，运行任务数仍为 0。注册和提交失败都不会终止服务端。

### 运行网络调度与模拟完成闭环

启动一个全新的服务端，在另一个终端依次执行：

```bash
./build/worker_client 1 2 4096
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

控制客户端支持 `next` 和 `complete <jobId>`。`complete` 只检查参数数量，Job ID 文本交给服务端校验。每次请求后连接关闭。

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

分配成功时客户端打印 `Scheduled Job 101 on Worker 1`，没有调度结果时打印 `No schedulable Job` 并正常退出；完成成功时打印 `Job completion accepted`。服务端返回错误时，客户端打印 `Scheduling failed: <错误码>` 或 `Completion failed: <错误码>`，并以非零状态退出。

`complete 102` 在 Job 102 尚未调度时会被拒绝；`complete -1`、`complete abc` 或超出 `int` 范围的 ID 返回 `INVALID_MESSAGE`。重复完成不会再次增加资源，已完成的 Job ID 仍不能重新提交。完成后不会自动调度下一项，需再次执行 `next`。

本演示的“运行”表示 Scheduler 中存在资源分配记录，完成由控制客户端手动模拟，不证明远程 Worker 实际执行了任务。

## TCP 消息协议

Worker 和 Job 客户端将三个参数用空格拼接，控制客户端发送调度或完成请求：

```text
REGISTER_WORKER <workerId> <cpuCores> <memoryMB>\n
SUBMIT_JOB <jobId> <cpuCores> <memoryMB>\n
SCHEDULE_NEXT\n
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
| 命令名 | `REGISTER_WORKER`、`SUBMIT_JOB`、`SCHEDULE_NEXT`、`COMPLETE_JOB`，区分大小写，精确匹配 |
| 控制请求参数 | `SCHEDULE_NEXT` 无参数；`COMPLETE_JOB` 恰好一个非负整数 ID，拒绝额外内容 |
| 非法输入 | 拒绝缺失字段、非整数、整数溢出、非法资源和额外非空白内容 |
| 重复 ID | 同一个 Scheduler 实例内分别拒绝重复 Worker ID 和 Job ID；两种 ID 相互独立 |

注册和 Job 提交解析器均使用字符串流提取整数，允许多余空白和数字字段紧邻正号的写法。以下注册正文均解析为 ID `1`、CPU `8`、内存 `16384 MB`：

```text
REGISTER_WORKER 1+8+16384
REGISTER_WORKER 1 8+16384
REGISTER_WORKER 1+8 16384
```

Job 解析器同样允许 `SUBMIT_JOB 101+2+4096`，解析结果为 Job ID `101`、CPU `2`、内存 `4096 MB`。这里的正号属于整数的符号，不执行加法运算。两个客户端使用空格分隔三个命令行参数。

完成解析器允许 `COMPLETE_JOB +101` 和多余空白，但只接受一个完整整数，`COMPLETE_JOB 101+102` 会被拒绝。

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
| `COMPLETE_OK\n` | 完成处理成功，资源已释放且运行记录已删除 |
| `COMPLETE_ERROR INVALID_MESSAGE\n` | 完成请求缺少 ID、ID 非法或含额外字段 |
| `COMPLETE_ERROR COMPLETION_REJECTED\n` | `completeJob()` 返回 false，例如 Job 未运行、已完成或释放失败 |
| `REQUEST_ERROR UNKNOWN_COMMAND\n` | 命令未被识别，例如 `SUBMIT_JOB_EXTRA` 或空行 |

`INVALID_MESSAGE` 目前不进一步区分非整数、负 ID 或非正资源。Worker 客户端识别 `REGISTER_OK` 和 `REGISTER_ERROR ` 前缀，Job 客户端识别 `SUBMIT_OK` 和 `SUBMIT_ERROR ` 前缀；其他响应分别报告 `Unexpected registration response` 或 `Unexpected submit response`，并以非零状态退出。

控制客户端按发出的操作识别响应。`next` 只接受合法 `SCHEDULE_OK`、精确的 `SCHEDULE_NONE` 或 `SCHEDULE_ERROR <错误码>`；`complete` 只接受精确的 `COMPLETE_OK` 或 `COMPLETE_ERROR <错误码>`。调度结果必须恰好包含两个空白分隔的完整非负整数字段，拒绝溢出、数字后缀、缺失或额外字段。错误响应必须恰好包含一个错误码；跨操作的响应也会被拒绝。

若连接在换行前结束，服务端报告 `Incomplete message`；正文超过长度限制时报告 `message too long`。这两类接收失败不发送业务结果，服务端关闭该客户端连接并继续等待后续连接。完整行命令被识别但内容无效时，按业务类型返回 `INVALID_MESSAGE`；重复 Worker 或 Job ID 则分别返回 `WORKER_REJECTED` 或 `JOB_REJECTED`。未知命令直接返回 `REQUEST_ERROR UNKNOWN_COMMAND`。收发失败也只结束当前请求；`accept()` 被 `EINTR` 打断时重试，其他 `accept()` 错误会关闭监听 socket 并结束服务端。

服务端在 `accept()` 成功后，对当前客户端 socket 设置 `SO_RCVTIMEO` 为 5 秒。接收调用等待期间没有数据可读并达到超时后，服务端报告 `Receive timed out`，关闭当前连接，不发送业务结果，然后继续接受连接。该配置限制单次 `recv()` 的阻塞时间，不是整条请求消息的总耗时限制；客户端持续缓慢发送字节时，整条消息仍可能耗时超过 5 秒。三个客户端等待响应时尚未设置接收超时。

状态修改先于响应发送。发送失败不会回滚注册、入队、分配或资源释放，也未缓存请求结果；调用方不能仅凭未收到响应判断操作是否发生。例如自动重试 `SCHEDULE_NEXT` 可能分配另一个 Job。当前演示不自动重试这些请求。

## 运行测试

```bash
ctest --test-dir build --output-on-failure
```

CTest 包含六个测试程序，也可以分别运行：

```bash
./build/scheduler_tests
./build/registration_tests
./build/tcp_io_tests
./build/job_submission_tests
./build/job_completion_tests
./build/scheduler_protocol_tests
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

`scheduler_protocol_tests` 直接调用服务端使用的 `processSchedulerRequest()`，覆盖四类命令分流、无 Worker/空队列/容量不足、严格 FIFO、提交→调度→完成→资源恢复的状态转换、重复完成、完成等待中或未知 Job，以及非法请求不修改状态。另覆盖客户端使用的调度结果解析函数，包括合法边界和畸形响应。

当前 CTest 自动验证调度核心、协议解析与请求分流、状态转换和字节流收发，不启动 TCP 服务端与客户端，也不验证 TCP 连接建立、服务端 5 秒超时配置或控制客户端进程的全部响应分支。这些流程可按上述步骤手动验证；发送函数的部分发送和 `EINTR` 重试分支尚未由测试专门触发。

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

- TCP 服务端串行处理连接，同一时刻只处理一个客户端的一条请求消息
- 单次接收等待最多约 5 秒，尚无整个请求的截止时间；持续缓慢发送字节的客户端仍可能长时间阻塞后续处理
- Worker 状态、等待 Job 与运行记录仅保存在内存中，服务端退出后消失
- 客户端退出不会注销 Worker，已登记的资源只是状态记录，尚未用于远程任务执行
- 地址和端口固定在代码中，客户端 Worker/Job ID 和资源信息通过命令行传入
- 网络 Job 需手动触发调度和模拟完成；尚未实现 Worker 任务分发、实际执行和执行结果回报
- 状态修改与响应发送不是原子操作，尚无请求结果缓存或自动恢复机制
- Scheduler 使用一把粗粒度 mutex，尚未研究锁竞争
- Job 只是模拟的资源请求，不执行真实工作负载
- 没有长期运行的 Worker 线程或自动调度循环
- 没有持久化和已完成 Job 历史
- 没有心跳、故障检测和 Job 重试
- 没有 GPU、GPU 显存或 NPU 资源模型
- Benchmark 使用小规模合成负载，不代表生产环境性能

## 后续计划

- 完善注册失败原因反馈和可配置的连接地址
- 扩展多个 Worker 的并发连接管理与连接生命周期处理
- 实现网络 Job 分发和任务执行结果回报
- 添加 Worker 心跳与故障检测
- 添加 Job 失败重试和重新调度
- 扩展 GPU、GPU 显存和 NPU 资源约束
- 增加更完整的调度实验和指标
