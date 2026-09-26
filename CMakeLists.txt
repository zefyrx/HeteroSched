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