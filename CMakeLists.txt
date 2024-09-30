# ##############################################################################
#
# cmake settings
#
# ##############################################################################

cmake_minimum_required(VERSION 3.20...3.28)
project(Tina VERSION 0.0.1 LANGUAGES CXX)
  
# Cache the root directory
set(ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR} CACHE PATH "Root directory path")
set(BUILD_DIR ${CMAKE_CURRENT_SOURCE_DIR}/build CACHE PATH "Build directory path")


option(TINA_BUILD_WAYLAND "Build Wayland" ON)

list(APPEND CMAKE_MODULE_PATH ${ROOT_DIR}/cmake)


# ##############################################################################
#
# project settings
#
# ##############################################################################

if (NOT CMAKE_BUILD_TYPE)
    message(STATUS "Setting build type to 'Release' as none was specified.")
    set(CMAKE_BUILD_TYPE
            Release
            CACHE STRING "Choose the type of build." FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release")
endif ()

include(CheckCXXCompilerFlag)
 
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)


# Import the tool cmake script
include(TinaTool)

add_subdirectory(dependencies)

add_executable(${PROJECT_NAME} runtime/src/main.cpp)

target_link_libraries(${PROJECT_NAME} PRIVATE ${TINA_LIBRARIES})
