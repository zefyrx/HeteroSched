#ifndef HETEROSCHED_REGISTRATION_H
#define HETEROSCHED_REGISTRATION_H

#include <optional>
#include <string>

#include "worker.h"

std::optional<Worker> parseRegistration(const std::string& line);

#endif