# Thin wrapper so AGP's externalNativeBuild does not point at the repository root.
#
# Pointing it at the root makes AGP pick up the sibling CMakeSettings.json, which is a Visual Studio
# file with a UTF-8 BOM. AGP parses that path as its own JSON schema and fails configuration with
# "Expected BEGIN_OBJECT but was STRING". That file is legitimate and shared, so the wrapper lives
# here instead of changing it.
cmake_minimum_required(VERSION 3.25)

# vcpkg supplies xxHash and mikktspace, so its toolchain must run and chainload the NDK's rather than
# the other way round. AGP hands us CMAKE_TOOLCHAIN_FILE pointing straight at the NDK, so that value
# is moved aside into VCPKG_CHAINLOAD_TOOLCHAIN_FILE and vcpkg is put in front.
#
# This happens before project(), because the toolchain file is only read at that point.
if(NOT DEFINED VCPKG_CHAINLOAD_TOOLCHAIN_FILE)
    if(NOT DEFINED ENV{VCPKG_ROOT})
        message(FATAL_ERROR
            "VCPKG_ROOT must be set for the Android build: vcpkg provides xxHash and mikktspace. "
            "Export it, or add vcpkg.dir to android/local.properties (see docs/building.md).")
    endif()
    set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_TOOLCHAIN_FILE}" CACHE FILEPATH "" FORCE)

    # The triplet has to follow ANDROID_ABI: AGP configures each ABI in its own build directory, so a
    # single hard-coded triplet would give one of them libraries built for the other architecture.
    if(ANDROID_ABI STREQUAL "arm64-v8a")
        set(VCPKG_TARGET_TRIPLET "arm64-android" CACHE STRING "" FORCE)
    elseif(ANDROID_ABI STREQUAL "x86_64")
        set(VCPKG_TARGET_TRIPLET "x64-android" CACHE STRING "" FORCE)
    else()
        # Refused rather than guessed: only these two ABIs have ever been verified, and a wrong
        # triplet fails deep inside dependency resolution where the cause is not obvious.
        message(FATAL_ERROR
            "Unsupported ANDROID_ABI '${ANDROID_ABI}'. Only arm64-v8a and x86_64 are verified; add a "
            "triplet mapping here after verifying the new ABI.")
    endif()

    # vcpkg only auto-detects a manifest at CMAKE_SOURCE_DIR, which this wrapper made android/app.
    # Without pointing it at the repository root it silently skips manifest mode entirely and the
    # first find_package fails with a missing-config error that says nothing about vcpkg.
    get_filename_component(TINA_REPOSITORY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
    set(VCPKG_MANIFEST_DIR "${TINA_REPOSITORY_ROOT}" CACHE PATH "" FORCE)

    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE FILEPATH "" FORCE)
endif()

project(TinaAndroidNative LANGUAGES C CXX)

# The engine tree is added as a subdirectory rather than duplicating any of its configuration, so
# there is exactly one definition of every target and option.
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../.. tina)
