#ifndef HETEROSCHED_SCHEDULER_PROTOCOL_H
#define HETEROSCHED_SCHEDULER_PROTOCOL_H

#include <optional>
#include <string>

#include "core/scheduler.h"

// 正文不含换行；先校验再修改状态，返回带换行的响应。
std::string processSchedulerRequest(const std::string& line, Scheduler& scheduler);

// 客户端只接受命令和两个完整的非负整数字段。
std::optional<ScheduleResult> parseScheduleResult(const std::string& line);

#endif
