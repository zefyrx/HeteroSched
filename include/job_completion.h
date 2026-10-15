#ifndef HETEROSCHED_JOB_COMPLETION_H
#define HETEROSCHED_JOB_COMPLETION_H

#include <optional>
#include <string>

std::optional<int> parseJobCompletion(const std::string& line);

#endif
