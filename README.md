
# HeteroSched

HeteroSched is a C++ learning project for building a heterogeneous-resource
scheduler step by step.

## Current Status

Day 1 is complete:

- CPU and memory resource representation
- basic Job model
- Worker resource feasibility checks
- resource allocation and release

The Scheduler, job queue, networking, and heterogeneous accelerators are not
implemented yet.

## Build

```bash
mkdir -p build
cd build
cmake ..
cmake --build .
./heterosched
```
