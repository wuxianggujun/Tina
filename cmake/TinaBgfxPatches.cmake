# Applies Tina's own patches to the vendored bgfx working tree.
#
# Why a patch file rather than edits in place: bgfx is a submodule of the
# bgfx.cmake submodule, so Tina records only a commit SHA for it and never the
# file contents. Edits made directly in thirdparty/bgfx.cmake/bgfx survive in
# one working tree and vanish on a fresh clone -- and the Android Vulkan present
# path fails as a black SurfaceView with audio still running, which reads like a
# renderer bug rather than a missing patch. Upstream is bkaradzic/bgfx, so the
# changes cannot be pushed there either.
#
# Applied on every platform, not just Android, so one source tree does not
# compile differently per target. The patch's own content is guarded by
# BX_PLATFORM_ANDROID, so it is inert elsewhere.

set(TINA_BGFX_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/bgfx.cmake/bgfx")
set(TINA_BGFX_PATCH_DIR "${CMAKE_CURRENT_SOURCE_DIR}/patches")

set(TINA_BGFX_PATCHES
    bgfx-android-vulkan-present.patch
)

# Only Android's correctness depends on these patches, so an unpatchable tree is
# fatal there and a warning elsewhere. Silently continuing on Android would ship
# a binary that presents nothing.
if(ANDROID)
    set(TINA_BGFX_PATCH_FAILURE_LEVEL FATAL_ERROR)
else()
    set(TINA_BGFX_PATCH_FAILURE_LEVEL WARNING)
endif()

find_package(Git QUIET)
if(NOT GIT_FOUND)
    message(${TINA_BGFX_PATCH_FAILURE_LEVEL}
        "Git was not found, so Tina's bgfx patches cannot be applied. "
        "Android Vulkan present requires ${TINA_BGFX_PATCHES}.")
    return()
endif()

foreach(patch_name IN LISTS TINA_BGFX_PATCHES)
    set(patch_file "${TINA_BGFX_PATCH_DIR}/${patch_name}")
    if(NOT EXISTS "${patch_file}")
        message(${TINA_BGFX_PATCH_FAILURE_LEVEL} "Missing bgfx patch: ${patch_file}")
        continue()
    endif()

    # Reverse-check first: it succeeds only when the patch is already fully
    # applied, which is the steady state for every reconfigure of an existing
    # tree. Checked before the forward apply because a second forward apply
    # would fail and look like a conflict.
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" apply --check --reverse "${patch_file}"
        WORKING_DIRECTORY "${TINA_BGFX_SOURCE_DIR}"
        RESULT_VARIABLE already_applied
        OUTPUT_QUIET ERROR_QUIET
    )
    if(already_applied EQUAL 0)
        message(STATUS "bgfx patch already applied: ${patch_name}")
        continue()
    endif()

    execute_process(
        COMMAND "${GIT_EXECUTABLE}" apply --check "${patch_file}"
        WORKING_DIRECTORY "${TINA_BGFX_SOURCE_DIR}"
        RESULT_VARIABLE can_apply
        ERROR_VARIABLE check_error
        OUTPUT_QUIET
    )
    if(NOT can_apply EQUAL 0)
        # Neither cleanly appliable nor already applied. A bgfx version bump is
        # the expected cause; a half-applied tree also lands here. Both need a
        # person, so this stops rather than guessing.
        message(${TINA_BGFX_PATCH_FAILURE_LEVEL}
            "bgfx patch ${patch_name} does not apply to ${TINA_BGFX_SOURCE_DIR} "
            "and is not already applied. Refresh it against the current bgfx "
            "revision. git reported:\n${check_error}")
        continue()
    endif()

    execute_process(
        COMMAND "${GIT_EXECUTABLE}" apply "${patch_file}"
        WORKING_DIRECTORY "${TINA_BGFX_SOURCE_DIR}"
        RESULT_VARIABLE applied
        ERROR_VARIABLE apply_error
    )
    if(NOT applied EQUAL 0)
        message(${TINA_BGFX_PATCH_FAILURE_LEVEL}
            "Applying bgfx patch ${patch_name} failed. git reported:\n${apply_error}")
        continue()
    endif()
    message(STATUS "Applied bgfx patch: ${patch_name}")
endforeach()
