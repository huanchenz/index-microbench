cmake_minimum_required (VERSION 2.8.11)
project(index-microbench)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/Modules/")

set(CMAKE_CXX_FLAGS "-std=c++11 -Wno-deprecated-declarations -Wall")

find_package(PAPI)
include_directories(${PAPI_INCLUDE_DIRS})

set(SHARED_DEPENDENCIES tcmalloc_minimal ${PAPI_LIBRARIES})
set(SHARED_SOURCES index.h indexkey.h allocatortracker.h microbench.h)

add_executable(workload workload.cpp ${SHARED_SOURCES})
target_link_libraries(workload ${SHARED_DEPENDENCIES})

add_executable(workload_string workload_string.cpp ${SHARED_SOURCES})
target_link_libraries(workload_string ${SHARED_DEPENDENCIES} pthread m)