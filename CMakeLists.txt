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
option(TINA_BUILD_LEGACY "Build Legacy product (retired; must stay OFF)" OFF)
option(TINA_BUILD_PLATFORM_GLFW "Build the private vNext GLFW platform backend" OFF)
option(TINA_BUILD_RENDER_BGFX "Build the private vNext bgfx render backend" OFF)
option(TINA_BUILD_UI_FREETYPE "Build the private vNext FreeType UI text adapter" OFF)
option(TINA_BUILD_UI_UIA "Build the private Windows UIA accessibility adapter" OFF)
option(TINA_BUILD_PHYSICS2D "Build the optional vNext Box2D physics module" OFF)
option(TINA_BUILD_AUDIO_MINIAUDIO "Build the private vNext miniaudio audio adapter" OFF)
option(TINA_AUDIO_ENABLE_LIBVORBIS "Enable Ogg Vorbis decoding via libvorbis (optional)" OFF)
option(TINA_AUDIO_ENABLE_LIBOPUS "Enable Opus decoding via libopus (optional)" OFF)
option(TINA_BUILD_BENCHMARKS "Build Tina benchmark executables" OFF)
option(TINA_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(TINA_AUTOUPDATE_SUBMODULE "Auto Update Github Submodules" OFF)
option(TINA_BUILD_WAYLAND "Build Wayland" OFF)

set(TINA_TRACE_BACKEND "none" CACHE STRING "Compile-time Tina trace backend")
set_property(CACHE TINA_TRACE_BACKEND PROPERTY STRINGS "none")
if (NOT TINA_TRACE_BACKEND STREQUAL "none")
    message(FATAL_ERROR
        "Unsupported TINA_TRACE_BACKEND='${TINA_TRACE_BACKEND}'. "
        "TRACE-001-A0 supports only 'none'.")
endif ()

if (TINA_BUILD_PLATFORM_GLFW)
    if (NOT DEFINED VCPKG_MANIFEST_FEATURES OR
        NOT "platform-glfw" IN_LIST VCPKG_MANIFEST_FEATURES)
        message(FATAL_ERROR
            "TINA_BUILD_PLATFORM_GLFW requires the vcpkg manifest feature 'platform-glfw'.")
    endif ()
endif ()

if (TINA_BUILD_UI_FREETYPE)
    if (NOT DEFINED VCPKG_MANIFEST_FEATURES OR
        NOT "ui-freetype" IN_LIST VCPKG_MANIFEST_FEATURES)
        message(FATAL_ERROR
            "TINA_BUILD_UI_FREETYPE requires the vcpkg manifest feature 'ui-freetype'.")
    endif ()
endif ()

if (TINA_BUILD_UI_UIA AND NOT WIN32)
    message(FATAL_ERROR "TINA_BUILD_UI_UIA is Windows-only.")
endif ()

if (TINA_BUILD_PHYSICS2D)
    if (NOT DEFINED VCPKG_MANIFEST_FEATURES OR
        NOT "physics2d" IN_LIST VCPKG_MANIFEST_FEATURES)
        message(FATAL_ERROR
            "TINA_BUILD_PHYSICS2D requires the vcpkg manifest feature 'physics2d'.")
    endif ()
endif ()

if (TINA_BUILD_AUDIO_MINIAUDIO)
    if (NOT DEFINED VCPKG_MANIFEST_FEATURES OR
        NOT "audio-miniaudio" IN_LIST VCPKG_MANIFEST_FEATURES)
        message(FATAL_ERROR
            "TINA_BUILD_AUDIO_MINIAUDIO requires the vcpkg manifest feature 'audio-miniaudio'.")
    endif ()
endif ()

if (TINA_AUDIO_ENABLE_LIBVORBIS)
    if (NOT TINA_BUILD_AUDIO_MINIAUDIO)
        message(FATAL_ERROR
            "TINA_AUDIO_ENABLE_LIBVORBIS requires TINA_BUILD_AUDIO_MINIAUDIO.")
    endif ()
    if (NOT DEFINED VCPKG_MANIFEST_FEATURES OR
        NOT "audio-miniaudio-vorbis" IN_LIST VCPKG_MANIFEST_FEATURES)
        message(FATAL_ERROR
            "TINA_AUDIO_ENABLE_LIBVORBIS requires vcpkg feature 'audio-miniaudio-vorbis'.")
    endif ()
endif ()

if (TINA_AUDIO_ENABLE_LIBOPUS)
    if (NOT TINA_BUILD_AUDIO_MINIAUDIO)
        message(FATAL_ERROR
            "TINA_AUDIO_ENABLE_LIBOPUS requires TINA_BUILD_AUDIO_MINIAUDIO.")
    endif ()
    if (NOT DEFINED VCPKG_MANIFEST_FEATURES OR
        NOT "audio-miniaudio-opus" IN_LIST VCPKG_MANIFEST_FEATURES)
        message(FATAL_ERROR
            "TINA_AUDIO_ENABLE_LIBOPUS requires vcpkg feature 'audio-miniaudio-opus'.")
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
    # Keep installed static libraries free of absolute producer checkout paths so
    # cross-distro SDK consumers can reject source/build path leakage in .a files.
    if(NOT PROJECT_SOURCE_DIR STREQUAL "")
        target_compile_options(tina_project_options INTERFACE
            "SHELL:-ffile-prefix-map=${PROJECT_SOURCE_DIR}=."
            "SHELL:-fdebug-prefix-map=${PROJECT_SOURCE_DIR}=."
        )
    endif()
    if(NOT CMAKE_BINARY_DIR STREQUAL "" AND NOT CMAKE_BINARY_DIR STREQUAL PROJECT_SOURCE_DIR)
        target_compile_options(tina_project_options INTERFACE
            "SHELL:-ffile-prefix-map=${CMAKE_BINARY_DIR}=./out/build"
            "SHELL:-fdebug-prefix-map=${CMAKE_BINARY_DIR}=./out/build"
        )
    endif()
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

# TINA_BUILD_LEGACY is a permanent reject switch; the product graph is gone.
# vNext samples stage cooked assets themselves; do not scan/copy source resources/.
if(TINA_BUILD_LEGACY)
    message(FATAL_ERROR
        "TINA_BUILD_LEGACY is retired and must stay OFF. "
        "Build vNext presets only (windows-msvc-vnext* / linux-*). "
        "There is no Legacy Tina.exe, old UI product graph, or recoverable Legacy dependency set.")
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
include(TinaUiFont)

if(TINA_BUILD_RENDER_BGFX OR TINA_BUILD_SHADERS)
    add_subdirectory(thirdparty)
endif()

add_subdirectory(src/core)
add_subdirectory(src/integration)
add_subdirectory(src/platform)
add_subdirectory(src/task)
add_subdirectory(src/render)
add_subdirectory(src/scene)
if (TINA_BUILD_PHYSICS2D)
    add_subdirectory(src/physics2d)
endif ()
add_subdirectory(src/asset_format)
add_subdirectory(src/asset)
add_subdirectory(src/ui)
add_subdirectory(src/audio)
if (TINA_BUILD_AUDIO_MINIAUDIO)
    add_subdirectory(src/audio/miniaudio)
endif ()
add_subdirectory(src/runtime)
if(TINA_BUILD_PLATFORM_GLFW AND TINA_BUILD_RENDER_BGFX)
    add_subdirectory(src/desktop)
endif()

include(TinaGameSdkPackage)
tina_configure_game_sdk_package()

# Examples and small host tools (catalog validate CLI, samples).
if (TINA_BUILD_EXAMPLES)
    add_subdirectory(samples)
    add_subdirectory(tools)

endif ()

# Tests
if (TINA_BUILD_TESTING)
    add_subdirectory(tests)
endif ()

