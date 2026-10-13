cmake_minimum_required(VERSION 3.16)

project(HeteroSched LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_library(heterosched_core
    src/resource.cpp
    src/job.cpp
    src/worker.cpp
    src/scheduler.cpp
    src/registration.cpp
    src/tcp_io.cpp
)

target_include_directories(heterosched_core PUBLIC include)

add_executable(heterosched
    src/main.cpp
)

target_link_libraries(heterosched
    PRIVATE
        heterosched_core
        Threads::Threads
)

enable_testing()

add_executable(scheduler_tests
    tests/scheduler_test.cpp
)

target_link_libraries(scheduler_tests PRIVATE heterosched_core Threads::Threads)

add_test(NAME scheduler_tests COMMAND scheduler_tests)

add_executable(scheduler_benchmark
    benchmarks/scheduler_benchmark.cpp
)

target_link_libraries(
    scheduler_benchmark
    PRIVATE
        heterosched_core
)

add_executable(scheduler_server
    src/scheduler_server.cpp
)

target_link_libraries(scheduler_server
    PRIVATE
        heterosched_core
        Threads::Threads
)

add_executable(worker_client
    src/worker_client.cpp
)

target_link_libraries(worker_client
    PRIVATE
        heterosched_core
)

add_executable(registration_tests
    tests/registration_test.cpp
)

target_link_libraries(registration_tests
    PRIVATE
        heterosched_core
)

add_test(NAME registration_tests COMMAND registration_tests)

add_executable(tcp_io_tests
    tests/tcp_io_test.cpp
)

target_link_libraries(tcp_io_tests PRIVATE heterosched_core)

add_test(NAME tcp_io_tests COMMAND tcp_io_tests)
set_tests_properties(tcp_io_tests PROPERTIES TIMEOUT 10)
