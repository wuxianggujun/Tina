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

option(TINA_BUILD_EXAMPLES "Whether or not to build examples with this stack" "${PROJECT_IS_TOP_LEVEL}")
option(TINA_BUILD_DOCS "Whether or not to generate documentation" "${PROJECT_IS_TOP_LEVEL}")
option(TINA_BUILD_TESTING "Google Tests" ON)
option(TINA_AUTOUPDATE_SUBMODULE "Auto Update Github Submodules" ON)

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
include(TinaTool)

if(TINA_AUTOUPDATE_SUBMODULE)
include(UpdateSubmodules)
endif()


if(MSVC)
   add_compile_options(/MP /ZI /JMC)
endif()

add_subdirectory(dependencies)
add_subdirectory(engine)
add_subdirectory(runtime)
add_subdirectory(samples)

add_subdirectory(tests)
