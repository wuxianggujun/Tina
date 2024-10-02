# ##############################################################################
#
# cmake settings
#
# ##############################################################################

cmake_minimum_required(VERSION 3.20...3.28)
project(Tina VERSION 0.0.1 LANGUAGES CXX)

# Cache the root directory
set(ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR} CACHE PATH "Root directory path")

option(TINA_BUILD_WAYLAND "Build Wayland" ON)

include(CheckCXXCompilerFlag)

set(CMAKE_CXX_STANDARD 17)


if (TINA_BUILD_WAYLAND)
    add_definitions(-DGLFW_BUILD_WAYLAND=ON)
    add_definitions(-DGLFW_BUILD_X11=OFF)
    add_definitions(-DGLFW_USE_WAYLAND=ON)
    add_definitions(-DTINA_CONFIG_USE_WAYLAND=1)

    # 定义宏 TINA_CONFIG_USE_WAYLAND
#    add_definitions(-DTINA_CONFIG_USE_WAYLAND=ON)
endif (TINA_BUILD_WAYLAND)


#message(STATUS "TINA_CONFIG_USE_WAYLAND is set to: ${TINA_CONFIG_USE_WAYLAND}")

add_subdirectory(dependencies)

add_executable(${PROJECT_NAME} runtime/src/main.cpp)

target_link_libraries(${PROJECT_NAME} PRIVATE ${TINA_LIBRARIES})
