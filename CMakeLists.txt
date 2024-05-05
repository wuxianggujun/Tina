cmake_minimum_required(VERSION 3.28)
project(Tina VERSION 0.0.1 LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Cache the root directory
set(ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR} CACHE PATH "Root directory path")
set(BUILD_DIR ${CMAKE_CURRENT_SOURCE_DIR}/build CACHE PATH "Build directory path")

# Functional options
option(TINA_BUILD_EXAMPLES "Whether or not to build examples with this stack" "${PROJECT_IS_TOP_LEVEL}")
option(TINA_BUILD_DOCS "Whether or not to generate documentation" "${PROJECT_IS_TOP_LEVEL}")
option(TINA_BUILD_TESTING "Turn on Tina's Google Tests" ON)
option(TINA_AUTOUPDATE_SUBMODULE "Auto Update Github Submodules" OFF)

list(APPEND CMAKE_MODULE_PATH ${ROOT_DIR}/cmake)

message(STATUS "CMAKE_MODULE_PATH is ${CMAKE_MODULE_PATH}")

find_package(Git)
if (NOT GIT_FOUND)
    message(FATAL_ERROR "Git needs to be installed to pull the dependencies")
else ()
    string(REGEX MATCH "^[0-9]+[.][0-9]+" GIT_VERSION "${GIT_VERSION_STRING}")
    if ("${GIT_VERSION}" LESS "2.35")
        message(FATAL_ERROR "Git version of 2.35 is needed for patch application")
    endif ()
endif ()

# Import the tool cmake script
include(TinaTool)

# Automatically update submodule versions
if(TINA_AUTOUPDATE_SUBMODULE)
    include(UpdateSubmodules)
endif()

if(MSVC)
    #add_compile_options("/source-charset:utf-8")
    string(APPEND CMAKE_CXX_FLAGS " /Zc:__cplusplus /MP")
endif()

add_subdirectory(dependencies)
add_subdirectory(engine)
add_subdirectory(runtime)

# Examples
if(TINA_BUILD_EXAMPLES)
    add_subdirectory(samples)
endif()

# Tests
if(TINA_BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
