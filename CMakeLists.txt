cmake_minimum_required(VERSION 3.16)

project(HeteroSched LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

# Reusable modules: core has no protocol or socket dependency.
add_library(heterosched_core
    src/core/resource.cpp
    src/core/worker.cpp
    src/core/scheduler.cpp
)
target_include_directories(heterosched_core PUBLIC include)
target_link_libraries(heterosched_core PUBLIC Threads::Threads)

add_library(heterosched_net src/net/tcp_io.cpp)
target_include_directories(heterosched_net PUBLIC include)

add_library(heterosched_protocol
    src/protocol/registration.cpp
    src/protocol/job_submission.cpp
    src/protocol/job_completion.cpp
    src/protocol/scheduler_protocol.cpp
)
target_include_directories(heterosched_protocol PUBLIC include)
target_link_libraries(heterosched_protocol PUBLIC heterosched_core)

# Executable entry points. Binary names and output locations stay the same.
add_executable(heterosched apps/demo_main.cpp)
target_link_libraries(heterosched PRIVATE heterosched_core)

add_executable(scheduler_server apps/scheduler_server.cpp)
target_link_libraries(scheduler_server PRIVATE heterosched_protocol heterosched_net)

add_executable(worker_client apps/worker_client.cpp)
target_link_libraries(worker_client PRIVATE heterosched_protocol heterosched_net)

add_executable(job_client apps/job_client.cpp)
target_link_libraries(job_client PRIVATE heterosched_net)

add_executable(scheduler_client apps/scheduler_client.cpp)
target_link_libraries(scheduler_client PRIVATE heterosched_protocol heterosched_net)

add_executable(scheduler_benchmark benchmarks/scheduler_benchmark.cpp)
target_link_libraries(scheduler_benchmark PRIVATE heterosched_core)

# Tests are grouped by the module they exercise.
enable_testing()

add_executable(scheduler_tests tests/core/scheduler_test.cpp)
target_link_libraries(scheduler_tests PRIVATE heterosched_core)
add_test(NAME scheduler_tests COMMAND scheduler_tests)

add_executable(job_dispatch_tests tests/core/job_dispatch_test.cpp)
target_link_libraries(job_dispatch_tests PRIVATE heterosched_core)
add_test(NAME job_dispatch_tests COMMAND job_dispatch_tests)
set_tests_properties(job_dispatch_tests PROPERTIES TIMEOUT 10)

add_executable(tcp_io_tests tests/net/tcp_io_test.cpp)
target_link_libraries(tcp_io_tests PRIVATE heterosched_net)
add_test(NAME tcp_io_tests COMMAND tcp_io_tests)
set_tests_properties(tcp_io_tests PROPERTIES TIMEOUT 10)

add_executable(registration_tests tests/protocol/registration_test.cpp)
target_link_libraries(registration_tests PRIVATE heterosched_protocol)
add_test(NAME registration_tests COMMAND registration_tests)

add_executable(job_submission_tests tests/protocol/job_submission_test.cpp)
target_link_libraries(job_submission_tests PRIVATE heterosched_protocol)
add_test(NAME job_submission_tests COMMAND job_submission_tests)

add_executable(job_completion_tests tests/protocol/job_completion_test.cpp)
target_link_libraries(job_completion_tests PRIVATE heterosched_protocol)
add_test(NAME job_completion_tests COMMAND job_completion_tests)

add_executable(scheduler_protocol_tests tests/protocol/scheduler_protocol_test.cpp)
target_link_libraries(scheduler_protocol_tests PRIVATE heterosched_protocol)
add_test(NAME scheduler_protocol_tests COMMAND scheduler_protocol_tests)
