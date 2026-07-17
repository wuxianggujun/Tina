# ##############################################################################
#
# cmake settings
#
# ##############################################################################

cmake_minimum_required(VERSION 3.25...4.2)
project(Tina VERSION 0.0.1 LANGUAGES C CXX)

# Keep every MSVC translation unit on UTF-8, including bundled source dependencies.
# The C++ language standard remains target-scoped through Tina::ProjectOptions.
if (MSVC)
    add_compile_options(
        "$<$<COMPILE_LANGUAGE:C>:/utf-8>"
        "$<$<COMPILE_LANGUAGE:CXX>:/utf-8>"
    )
endif ()

if(POLICY CMP0175)
    cmake_policy(SET CMP0175 OLD)
endif()

# In-source builds are disabled.
if ("${CMAKE_CURRENT_SOURCE_DIR}" STREQUAL "${CMAKE_CURRENT_BINARY_DIR}")
    message(
            FATAL_ERROR
            "CMake generation is not possible within the source directory!"
            "\n Remove the CMakeCache.txt file and try again from another folder, "
            "e.g.:\n "
            "\n rm CMakeCache.txt"
            "\n mkdir build"
            "\n cd build"
            "\n cmake ..")
endif ()
# Cache the root directory
set(ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR} CACHE PATH "Root directory path")
set(BUILD_DIR ${CMAKE_CURRENT_SOURCE_DIR}/build CACHE PATH "Build directory path")
set(RESOURCE_DIR ${ROOT_DIR}/resources CACHE PATH "Resource directory path")
set(RESOURCE_DEST_DIR ${CMAKE_BINARY_DIR}/resources CACHE PATH "Resource directory path")
# 统一可执行输出目录到构建目录/bin（强制覆盖缓存，避免沿用旧值指向源码根）
set(BIN_DIR ${CMAKE_BINARY_DIR}/bin CACHE PATH "Binary output directory path" FORCE)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${BIN_DIR})
if (CMAKE_CONFIGURATION_TYPES)
    foreach(cfg ${CMAKE_CONFIGURATION_TYPES})
        string(TOUPPER "${cfg}" cfg_up)
        # Debug/Release binaries and app-local DLLs are ABI-incompatible. Keep
        # every executable, including Tina and shaderc, in its own config dir.
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${cfg_up} ${BIN_DIR}/${cfg})
    endforeach()
endif()
message(STATUS "BIN_DIR = ${BIN_DIR}")

# Functional options
option(TINA_BUILD_EXAMPLES "Whether or not to build examples with this stack" "${PROJECT_IS_TOP_LEVEL}")
option(TINA_BUILD_DOCS "Whether or not to generate documentation" "${PROJECT_IS_TOP_LEVEL}")
option(TINA_BUILD_TESTING "Build Tina tests" ON)
option(TINA_BUILD_SHADERS "Build cooked runtime shaders with the bundled shader compiler" ON)
option(TINA_BUILD_LEGACY "Build the existing Tina game/runtime during vNext migration" ON)
option(TINA_BUILD_PLATFORM_GLFW "Build the private vNext GLFW platform backend" OFF)
option(TINA_BUILD_RENDER_BGFX "Build the private vNext bgfx render backend" OFF)
option(TINA_BUILD_BENCHMARKS "Build Tina benchmark executables" OFF)
option(TINA_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(TINA_AUTOUPDATE_SUBMODULE "Auto Update Github Submodules" OFF)
option(TINA_BUILD_WAYLAND "Build Wayland" OFF)

if (TINA_BUILD_PLATFORM_GLFW AND NOT TINA_BUILD_LEGACY)
    if (NOT DEFINED VCPKG_MANIFEST_FEATURES OR
        NOT "platform-glfw" IN_LIST VCPKG_MANIFEST_FEATURES)
        message(FATAL_ERROR
            "TINA_BUILD_PLATFORM_GLFW requires the vcpkg manifest feature 'platform-glfw'.")
    endif ()
endif ()

if (TINA_BUILD_WAYLAND)
    if (NOT DEFINED VCPKG_MANIFEST_FEATURES OR
        NOT "wayland" IN_LIST VCPKG_MANIFEST_FEATURES)
        message(FATAL_ERROR
            "TINA_BUILD_WAYLAND requires the vcpkg manifest feature 'wayland'. "
            "Use the linux-wayland preset or set VCPKG_MANIFEST_FEATURES=wayland.")
    endif ()
endif ()

list(APPEND CMAKE_MODULE_PATH ${ROOT_DIR}/cmake)

if (TINA_AUTOUPDATE_SUBMODULE)
    find_package(Git)
    if (NOT GIT_FOUND)
        message(FATAL_ERROR "Git needs to be installed to pull the dependencies")
    else ()
        string(REGEX MATCH "^[0-9]+[.][0-9]+" GIT_VERSION "${GIT_VERSION_STRING}")
        if ("${GIT_VERSION}" LESS "2.35")
            message(FATAL_ERROR "Git version of 2.35 is needed for patch application")
        endif ()
    endif ()
endif ()
# ##############################################################################
#
# project settings
#
# ##############################################################################

if (NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    message(STATUS "Setting build type to 'Release' as none was specified.")
    set(CMAKE_BUILD_TYPE
            Release
            CACHE STRING "Choose the type of build." FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release")
endif ()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Tina does not publish or consume C++ modules yet. CMake 4.x otherwise enables
# dependency scanning for some Clang C++23 toolchains and requires an optional
# clang-scan-deps executable even though this source graph contains no modules.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

add_library(tina_project_options INTERFACE)
add_library(Tina::ProjectOptions ALIAS tina_project_options)
target_compile_features(tina_project_options INTERFACE cxx_std_23)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(tina_project_options INTERFACE
        -Wall
        -Wextra
        "$<$<CONFIG:Release>:-O3>"
    )
elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    # Bound compiler-level parallelism so it does not multiply the MSBuild /m value from presets.
    target_compile_options(tina_project_options INTERFACE /MP2 /Zc:__cplusplus)
endif()

if (BUILD_SHARED_LIBS)
    set(CMAKE_CXX_VISIBILITY_PRESET hidden)
endif ()


# Automatically update submodule versions
if (TINA_AUTOUPDATE_SUBMODULE)
    include(UpdateSubmodules)
endif ()

# Legacy still consumes the source resource tree directly. Null/vNext-only configure must not scan
# or copy it; Cooked asset staging will be owned by the later Asset vertical slice.
if(TINA_BUILD_LEGACY)
    file(GLOB_RECURSE RESOURCE_FILES "${RESOURCE_DIR}/*")

    foreach (FILE ${RESOURCE_FILES})
        if (NOT FILE MATCHES "/shaders/")
            file(RELATIVE_PATH REL_PATH "${RESOURCE_DIR}" "${FILE}")
            get_filename_component(REAL_PATH "${REL_PATH}" DIRECTORY)
            file(MAKE_DIRECTORY "${RESOURCE_DEST_DIR}/${REAL_PATH}")
            file(COPY "${FILE}" DESTINATION "${RESOURCE_DEST_DIR}/${REAL_PATH}")
        endif ()
    endforeach ()
endif()

if(TINA_ENABLE_SANITIZERS)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT MSVC)
        target_compile_options(tina_project_options INTERFACE
            -fsanitize=address,undefined
            -fno-omit-frame-pointer
        )
        target_link_options(tina_project_options INTERFACE
            -fsanitize=address,undefined
        )
    else()
        message(FATAL_ERROR
            "TINA_ENABLE_SANITIZERS currently requires GCC or Clang on a Unix-like target")
    endif()
endif()

include(Dependencies)

if(TINA_BUILD_LEGACY OR TINA_BUILD_RENDER_BGFX OR TINA_BUILD_SHADERS)
    add_subdirectory(thirdparty)
endif()

add_subdirectory(src/core)
add_subdirectory(src/integration "${CMAKE_CURRENT_BINARY_DIR}/vnext/integration")

# vNext modules are independent from the Legacy product graph. Explicit binary
# directories keep src/platform and Legacy src/platform/audio from competing
# for the same nested build directory while both architectures coexist.
set(TINA_VNEXT_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/vnext")
add_subdirectory(src/platform "${TINA_VNEXT_BINARY_DIR}/platform")
add_subdirectory(src/task "${TINA_VNEXT_BINARY_DIR}/task")
add_subdirectory(src/render "${TINA_VNEXT_BINARY_DIR}/render")
add_subdirectory(src/vnext/ui "${TINA_VNEXT_BINARY_DIR}/ui")
add_subdirectory(src/runtime "${TINA_VNEXT_BINARY_DIR}/runtime")
if(TINA_BUILD_PLATFORM_GLFW AND TINA_BUILD_RENDER_BGFX)
    add_subdirectory(src/desktop "${TINA_VNEXT_BINARY_DIR}/desktop")
endif()

if(TINA_BUILD_LEGACY)
    add_subdirectory(src)
endif()

# Examples
if (TINA_BUILD_EXAMPLES)
    add_subdirectory(samples)
endif ()

# Tests
if (TINA_BUILD_TESTING)
    add_subdirectory(tests)
endif ()

