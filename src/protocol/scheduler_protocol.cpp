#include "protocol/scheduler_protocol.h"

#include <charconv>
#include <sstream>
#include <system_error>

#include "protocol/job_completion.h"
#include "protocol/job_submission.h"
#include "protocol/registration.h"

namespace {

std::optional<int> parseNonNegativeField(const std::string& field) {
    int value = 0;
    const char* end = field.data() + field.size();
    const auto result = std::from_chars(field.data(), end, value);
    if (result.ec != std::errc{} || result.ptr != end || value < 0) {
        return std::nullopt;
    }
    return value;
}

}  // namespace

std::string processSchedulerRequest(const std::string& line, Scheduler& scheduler) {
    std::istringstream input(line);
    std::string command;
    input >> command;

    if (command == "REGISTER_WORKER") {
        const auto worker = parseRegistration(line);
        if (!worker) {
            return "REGISTER_ERROR INVALID_MESSAGE\n";
        }
        if (!scheduler.addWorker(*worker)) {
            return "REGISTER_ERROR WORKER_REJECTED\n";
        }
        return "REGISTER_OK\n";
    }

    if (command == "SUBMIT_JOB") {
        const auto job = parseJobSubmission(line);
        if (!job) {
            return "SUBMIT_ERROR INVALID_MESSAGE\n";
        }
        if (!scheduler.submitJob(*job)) {
            return "SUBMIT_ERROR JOB_REJECTED\n";
        }
        return "SUBMIT_OK\n";
    }

    if (command == "SCHEDULE_NEXT") {
        std::string extra;
        if (input >> extra) {
            return "SCHEDULE_ERROR INVALID_MESSAGE\n";
        }

        const auto result = scheduler.scheduleNext();
        if (!result) {
            return "SCHEDULE_NONE\n";
        }
        return "SCHEDULE_OK " + std::to_string(result->jobId) + " " +
               std::to_string(result->workerId) + "\n";
    }

    if (command == "COMPLETE_JOB") {
        const auto jobId = parseJobCompletion(line);
        if (!jobId) {
            return "COMPLETE_ERROR INVALID_MESSAGE\n";
        }
        if (!scheduler.completeJob(*jobId)) {
            return "COMPLETE_ERROR COMPLETION_REJECTED\n";
        }
        return "COMPLETE_OK\n";
    }

    return "REQUEST_ERROR UNKNOWN_COMMAND\n";
}

std::optional<ScheduleResult> parseScheduleResult(const std::string& line) {
    std::istringstream input(line);
    std::string command;
    std::string jobField;
    std::string workerField;
    std::string extra;
    if (!(input >> command >> jobField >> workerField) ||
        command != "SCHEDULE_OK" || (input >> extra)) {
        return std::nullopt;
    }

    const auto jobId = parseNonNegativeField(jobField);
    const auto workerId = parseNonNegativeField(workerField);
    if (!jobId || !workerId) {
        return std::nullopt;
    }
    return ScheduleResult{*jobId, *workerId};
}
