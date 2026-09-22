#ifndef HETEROSCHED_JOB_H
#define HETEROSCHED_JOB_H

#include "resource.h"

struct Job {
    int jobId;
    Resource requiredResource;
};

#endif
