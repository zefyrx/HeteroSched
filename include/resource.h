#ifndef HETEROSCHED_RESOURCE_H
#define HETEROSCHED_RESOURCE_H

struct Resource {
    int cpuCores;
    int memoryMB;
};

bool hasPositiveResources(const Resource& resource);

#endif
