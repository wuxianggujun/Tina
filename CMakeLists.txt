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
    add_definitions(-DTINA_CONFIG_USE_WAYLAND=1)
   #add_definitions(-DBGFX_WITH_WAYLAND=ON)
   add_definitions(-DWL_EGL_PLATFORM=1)
#    add_compile_definitions(-lwayland-egl)

endif (TINA_BUILD_WAYLAND)

add_subdirectory(dependencies)

add_executable(${PROJECT_NAME} runtime/src/main.cpp)

if (TINA_BUILD_WAYLAND)
    include(FindPkgConfig)

    pkg_check_modules(Wayland REQUIRED
            wayland-client>=0.2.7
            wayland-cursor>=0.2.7
            wayland-egl>=0.2.7
            xkbcommon>=0.5.0)

    target_include_directories(${PROJECT_NAME} PRIVATE ${Wayland_INCLUDE_DIRS})

    if (NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        find_package(EpollShim)
        if (EPOLLSHIM_FOUND)
            target_include_directories(${PROJECT_NAME} PRIVATE ${EPOLLSHIM_INCLUDE_DIRS})
            target_link_libraries(${PROJECT_NAME} PRIVATE ${EPOLLSHIM_LIBRARIES})
        endif ()
    endif ()
endif (TINA_BUILD_WAYLAND)

if (TINA_BUILD_WAYLAND)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${Wayland_LIBRARIES})
#    target_link_libraries(${PROJECT_NAME} PRIVATE -lwayland-egl)
endif ()
target_link_libraries(${PROJECT_NAME} PRIVATE ${TINA_LIBRARIES})

