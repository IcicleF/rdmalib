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

# Check RNIC version
execute_process(
    COMMAND lspci
    COMMAND grep "Mellanox"
    OUTPUT_VARIABLE NIC_VER_CHECK
)
if (NIC_VER_CHECK MATCHES ".*\[ConnectX-[5-9]\].*")
else ()
    message(FATAL_ERROR "rdmalib only runs with Mellanox ConnectX-5+ RNICs. Abort.")
    return()
endif ()

# Compile library
add_library(rdmalib
    impl/context.cpp
    impl/cluster.cpp
    impl/peer.cpp

    impl/rc/rc.cpp
    impl/xrc/xrc.cpp
)

# Test application
option(TESTAPP "Compile an rdmalib test app" OFF)

if (TESTAPP)
    message(STATUS "[rdmalib] Compile test apps.")
    
    file(GLOB_RECURSE TESTAPPS test/*.cpp)
    foreach(test_filename IN LISTS TESTAPPS)
        string(REGEX REPLACE ".+/(.+)\\..*" "\\1" name ${test_filename})
        add_executable(${name} ${test_filename})
        target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR})
        target_link_libraries(${name} ${MPI_CXX_LIBRARIES} rdmalib ibverbs)
    endforeach()
else ()
endif ()
