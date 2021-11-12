cmake_minimum_required(VERSION 3.4)
project(Corneria)

find_package(MPI REQUIRED)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)
set(CMAKE_BUILD_TYPE RelWithDebInfo)
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -Wall -Wno-unused-variable -O3 -march=native")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Configure MPI
include_directories(SYSTEM ${MPI_INCLUDE_PATH})

add_library(rdmalib
    context.cpp
    cluster.cpp
    peer.cpp
    connection.cpp
)

add_executable(hello test/hello.cpp)
target_include_directories(hello PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(hello rdmalib ibverbs ${MPI_CXX_LIBRARIES})
