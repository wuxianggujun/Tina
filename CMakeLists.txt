cmake_minimum_required(VERSION 3.18..3.28)
project(Tina VERSION 0.0.1 LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Cache the root directory
set(ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR} CACHE PATH "Root directory path")

set(BUILD_DIR ${CMAKE_CURRENT_SOURCE_DIR}/build CACHE PATH "Build directory path")

list(APPEND CMAKE_MODULE_PATH ${ROOT_DIR}/cmake)

message(STATUS "CMAKE_MODULE_PATH is ${CMAKE_MODULE_PATH}")

include(TinaTool)

if(MSVC)
   add_compile_options(/MP /ZI /JMC)
endif()

add_subdirectory(dependencies)
add_subdirectory(engine)
add_subdirectory(runtime)
add_subdirectory(samples)

add_subdirectory(tests)
