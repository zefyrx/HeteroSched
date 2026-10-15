#ifndef HETEROSCHED_JOB_H
#define HETEROSCHED_JOB_H

#include "core/resource.h"

struct Job {
    int jobId;
    Resource requiredResource;
};

#endif
