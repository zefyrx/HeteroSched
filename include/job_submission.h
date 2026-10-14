#ifndef HETEROSCHED_JOB_SUBMISSION_H
#define HETEROSCHED_JOB_SUBMISSION_H

#include <optional>
#include <string>

#include "job.h"

std::optional<Job> parseJobSubmission(const std::string& line);

#endif
