cmake_minimum_required(VERSION 3.16)

project(HeteroSched LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(heterosched
    src/main.cpp
    src/worker.cpp
)

target_include_directories(heterosched PRIVATE include)
