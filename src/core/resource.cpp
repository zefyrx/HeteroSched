#include "core/resource.h"

bool hasPositiveResources(const Resource& resource) {
    if (!(resource.cpuCores > 0 && resource.memoryMB > 0)) {
        return false;
    }

    return true;
}
