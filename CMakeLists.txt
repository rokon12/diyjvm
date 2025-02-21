cmake_minimum_required(VERSION 3.30)
project(diyjvm C)

set(CMAKE_C_STANDARD 23)

add_executable(diyjvm src/main.c
        include/diyjvm.h)

target_include_directories(diyjvm PRIVATE include)

if (CMAKE_C_COMPILER_ID STREQUAL "GNU|Clang")
    target_compile_options(diyjvm PRIVATE -Wall -Wextra -Wpedantic)
endif ()