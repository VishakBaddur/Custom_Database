cmake_minimum_required(VERSION 3.15)
project(DistributedDB VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pthread")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")

# Find Boost with legacy finder
find_package(Boost REQUIRED)

include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${Boost_INCLUDE_DIRS})

# Database library
add_library(database_lib
    src/core/database.cpp
)

# Network library
add_library(network_lib
    src/network/protocol.cpp
    src/network/server.cpp
    src/network/client.cpp
)

target_link_libraries(network_lib database_lib ${Boost_LIBRARIES})

# Server executable
add_executable(distributeddb_server src/server_main.cpp)
target_link_libraries(distributeddb_server network_lib)

# Client executable
add_executable(distributeddb_client src/client_main.cpp)
target_link_libraries(distributeddb_client network_lib)

# Benchmark executable
add_executable(distributeddb_benchmark src/benchmark_main.cpp)
target_link_libraries(distributeddb_benchmark network_lib)

# Simple database executable (for testing)
add_executable(distributeddb src/main.cpp)
target_link_libraries(distributeddb database_lib)
