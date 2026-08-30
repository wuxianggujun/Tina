include_guard(GLOBAL)

# Which bgfx renderer binaries every embedded program is cooked for. Each entry is
# "suffix|shaderc platform|shaderc profile"; the suffix becomes both the generated symbol
# and the header name, so the tables in src/render/bgfx/*Shader.cpp must name the same
# suffixes.
#
# The suffixes are not ours to choose. bgfx's own bgfxToolUtils.cmake maps profiles to
# exactly these names in _bgfx_get_profile_ext(), commented "extensions consistent with
# embedded_shader.h": 120 -> glsl, 100_es/300_es -> essl, spirv -> spv, s_5_0 -> dxbc,
# metal -> mtl. Keep them aligned, and use mtl when Metal is added.
#
# `spv` is cooked with platform=linux on purpose, matching what bgfx does at
# bgfxToolUtils.cmake:650, where spirv forces PLATFORM_I=LINUX regardless of the target.
# It reads like an oversight in our list otherwise, and "fixing" it to windows/android
# walks into whatever that override exists to avoid.
#
# We deliberately do not call bgfx_compile_shaders() here, having read it:
#   - it writes ${OUTPUT_DIR}/${profile}/name.bin.h, while our tables include flat
#     name_suffix.bin.h and distinguish variants by symbol. Its layout also collapses
#     100_es and 300_es onto one `essl` directory, so both GLES tiers cannot coexist;
#   - it picks profiles from the *host* (IOS/ANDROID/WIN32 branches), which is the
#     opposite of what C5 needs: cooking android essl from a Windows host is the whole
#     point, so the ESSL path is compiled by a toolchain someone actually runs;
#   - it ties -O to the CMake config, and changing our fixed -O 3 would alter shader
#     binaries in Debug and move the product pixel fingerprints.
# What we take from it is the naming authority above, not the driver.
#
# The set is host-conditional today: dxbc only exists on Windows because shaderc's HLSL
# path needs the Windows SDK. Mobile profiles are opt-in via
# TINA_RENDER_BGFX_MOBILE_SHADERS so a desktop build does not pay for renderers it can
# never select -- but the option is available on desktop, because otherwise the ESSL
# branch would only ever be built by a toolchain nobody here runs, which is exactly how
# the deleted cmake/ShaderUtils.cmake came to claim Metal/GLES support it never had
# (ADR 0032 section 5).
function(tina_bgfx_shader_profiles OUT_VAR)
    set(PROFILES
        "glsl|linux|120"
        "spv|linux|spirv"
    )
    if(WIN32)
        list(APPEND PROFILES "dxbc|windows|s_5_0")
    endif()
    if(TINA_RENDER_BGFX_MOBILE_SHADERS)
        # OpenGL ES 3.0: the floor for ANativeWindow-era Android and for iOS GLES.
        # bgfx reports both as RendererType::OpenGLES, so one binary covers them.
        list(APPEND PROFILES "essl|android|300_es")
    endif()
    set("${OUT_VAR}" "${PROFILES}" PARENT_SCOPE)
endfunction()

# Resolves the shaderc to cook with, plus whatever the cook steps must depend on.
#
# shaderc is a BUILD-HOST tool: it turns shader source into headers during the build, and
# nothing ever runs it on the target. bgfx.cmake upstream declares it with a plain
# add_executable() and has no cross-compiling story, so a cross build aims it at the target
# and produces a binary the host cannot execute.
#
# Measured 2026-08-29 with an Android arm64-v8a probe (NDK 28): bx/bimg/bgfx cross-compile
# cleanly and every Tina .cpp in tina_render_bgfx compiles, but all 22 cook steps failed
# because bin/shaderc came out a 460 MB AArch64 ELF (llvm-readelf: Machine AArch64). Zero C++
# errors -- the whole failure surface was this one host/target confusion.
#
# Hence TINA_BGFX_SHADERC_EXECUTABLE for cross builds. Note what is NOT done here: the cook
# is never skipped when shaderc is unavailable. The generated headers are exactly what the
# RendererType tables in src/render/bgfx/*Shader.cpp include, so skipping would trade a clear
# build error for an obscure link error.
#
# OUT_COMMAND receives the executable to run; OUT_DEPENDS the DEPENDS entries. A native build
# depends on the bgfx::shaderc *target* so editing bgfx re-cooks; an imported host binary is a
# file dependency instead, since this tree cannot rebuild it.
function(_tina_resolve_bgfx_shaderc CALLER OUT_COMMAND OUT_DEPENDS)
    if(TINA_BGFX_SHADERC_EXECUTABLE)
        if(NOT EXISTS "${TINA_BGFX_SHADERC_EXECUTABLE}")
            message(FATAL_ERROR
                "${CALLER}: TINA_BGFX_SHADERC_EXECUTABLE is set to "
                "'${TINA_BGFX_SHADERC_EXECUTABLE}', which does not exist. It must be a shaderc "
                "built for the build host.")
        endif()
        set("${OUT_COMMAND}" "${TINA_BGFX_SHADERC_EXECUTABLE}" PARENT_SCOPE)
        set("${OUT_DEPENDS}" "${TINA_BGFX_SHADERC_EXECUTABLE}" PARENT_SCOPE)
        return()
    endif()

    if(CMAKE_CROSSCOMPILING)
        message(FATAL_ERROR
            "${CALLER}: shaderc runs on the build host, but this is a cross build targeting "
            "${CMAKE_SYSTEM_NAME}, so the in-tree shaderc cannot execute here. Set "
            "-DTINA_BGFX_SHADERC_EXECUTABLE=<path to a host-built shaderc> (for example the "
            "one in an existing desktop build tree's bin directory).")
    endif()
    if(NOT TARGET bgfx::shaderc)
        message(FATAL_ERROR
            "${CALLER}: bgfx::shaderc is required for the private bgfx backend")
    endif()
    set("${OUT_COMMAND}" "$<TARGET_FILE:bgfx::shaderc>" PARENT_SCOPE)
    set("${OUT_DEPENDS}" "bgfx::shaderc" PARENT_SCOPE)
endfunction()

function(tina_add_bgfx_embedded_shader TARGET SHADER_NAME VERTEX_SHADER FRAGMENT_SHADER VARYING_DEF)
    if(NOT TARGET "${TARGET}")
        message(FATAL_ERROR "tina_add_bgfx_embedded_shader: target '${TARGET}' does not exist")
    endif()
    _tina_resolve_bgfx_shaderc(
        "tina_add_bgfx_embedded_shader" SHADERC_COMMAND SHADERC_DEPENDS)

    set(SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/shaders/${SHADER_NAME}")
    set(SHADER_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/thirdparty/bgfx.cmake/bgfx/src")

    tina_bgfx_shader_profiles(SHADER_PROFILES)

    set(GENERATED_HEADERS)
    foreach(SHADER_STAGE IN ITEMS vertex fragment)
        if(SHADER_STAGE STREQUAL "vertex")
            set(SHADER_SOURCE "${VERTEX_SHADER}")
        else()
            set(SHADER_SOURCE "${FRAGMENT_SHADER}")
        endif()
        get_filename_component(SHADER_SYMBOL_BASE "${SHADER_SOURCE}" NAME_WE)

        foreach(SHADER_PROFILE_SPEC IN LISTS SHADER_PROFILES)
            string(REPLACE "|" ";" SHADER_PROFILE_PARTS "${SHADER_PROFILE_SPEC}")
            list(GET SHADER_PROFILE_PARTS 0 SHADER_SUFFIX)
            list(GET SHADER_PROFILE_PARTS 1 SHADER_PLATFORM)
            list(GET SHADER_PROFILE_PARTS 2 SHADER_PROFILE)

            set(SHADER_SYMBOL "${SHADER_SYMBOL_BASE}_${SHADER_SUFFIX}")
            set(SHADER_HEADER "${SHADER_OUTPUT_DIR}/${SHADER_SYMBOL}.bin.h")
            add_custom_command(
                OUTPUT "${SHADER_HEADER}"
                COMMAND "${CMAKE_COMMAND}" -E make_directory "${SHADER_OUTPUT_DIR}"
                COMMAND "${SHADERC_COMMAND}"
                    --type "${SHADER_STAGE}"
                    --platform "${SHADER_PLATFORM}"
                    --profile "${SHADER_PROFILE}"
                    --varyingdef "${VARYING_DEF}"
                    -i "${SHADER_INCLUDE_DIR}"
                    -i "${CMAKE_CURRENT_SOURCE_DIR}/bgfx/shaders"
                    -f "${SHADER_SOURCE}"
                    -o "${SHADER_HEADER}"
                    --bin2c "${SHADER_SYMBOL}"
                    --Werror
                    -O 3
                DEPENDS
                    ${SHADERC_DEPENDS}
                    "${SHADER_SOURCE}"
                    "${VARYING_DEF}"
                COMMENT "Compiling ${SHADER_NAME} ${SHADER_STAGE} shader for ${SHADER_PROFILE}"
                VERBATIM
            )
            list(APPEND GENERATED_HEADERS "${SHADER_HEADER}")
        endforeach()
    endforeach()

    set(SHADER_TARGET "${TARGET}_${SHADER_NAME}_shaders")
    add_custom_target("${SHADER_TARGET}" DEPENDS ${GENERATED_HEADERS})
    add_dependencies("${TARGET}" "${SHADER_TARGET}")
    set_source_files_properties(${GENERATED_HEADERS} PROPERTIES GENERATED TRUE)
    target_sources("${TARGET}" PRIVATE
        "${VERTEX_SHADER}"
        "${FRAGMENT_SHADER}"
        "${VARYING_DEF}"
        ${GENERATED_HEADERS}
    )
    target_include_directories("${TARGET}" PRIVATE "${SHADER_OUTPUT_DIR}")
endfunction()

# Vertex-only variant for programs that pair a new vertex stage with an
# already-embedded fragment shader (e.g. the skinned Opaque3D program reusing
# fs_tina_opaque3d_mr). The varying def must declare the same interpolators as
# the def the shared fragment shader was compiled with.
function(tina_add_bgfx_embedded_vertex_shader TARGET SHADER_NAME VERTEX_SHADER VARYING_DEF)
    if(NOT TARGET "${TARGET}")
        message(FATAL_ERROR "tina_add_bgfx_embedded_vertex_shader: target '${TARGET}' does not exist")
    endif()
    _tina_resolve_bgfx_shaderc(
        "tina_add_bgfx_embedded_vertex_shader" SHADERC_COMMAND SHADERC_DEPENDS)

    set(SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/shaders/${SHADER_NAME}")
    set(SHADER_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/thirdparty/bgfx.cmake/bgfx/src")

    tina_bgfx_shader_profiles(SHADER_PROFILES)

    get_filename_component(SHADER_SYMBOL_BASE "${VERTEX_SHADER}" NAME_WE)
    set(GENERATED_HEADERS)
    foreach(SHADER_PROFILE_SPEC IN LISTS SHADER_PROFILES)
        string(REPLACE "|" ";" SHADER_PROFILE_PARTS "${SHADER_PROFILE_SPEC}")
        list(GET SHADER_PROFILE_PARTS 0 SHADER_SUFFIX)
        list(GET SHADER_PROFILE_PARTS 1 SHADER_PLATFORM)
        list(GET SHADER_PROFILE_PARTS 2 SHADER_PROFILE)

        set(SHADER_SYMBOL "${SHADER_SYMBOL_BASE}_${SHADER_SUFFIX}")
        set(SHADER_HEADER "${SHADER_OUTPUT_DIR}/${SHADER_SYMBOL}.bin.h")
        add_custom_command(
            OUTPUT "${SHADER_HEADER}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${SHADER_OUTPUT_DIR}"
            COMMAND "${SHADERC_COMMAND}"
                --type vertex
                --platform "${SHADER_PLATFORM}"
                --profile "${SHADER_PROFILE}"
                --varyingdef "${VARYING_DEF}"
                -i "${SHADER_INCLUDE_DIR}"
                -i "${CMAKE_CURRENT_SOURCE_DIR}/bgfx/shaders"
                -f "${VERTEX_SHADER}"
                -o "${SHADER_HEADER}"
                --bin2c "${SHADER_SYMBOL}"
                --Werror
                -O 3
            DEPENDS
                ${SHADERC_DEPENDS}
                "${VERTEX_SHADER}"
                "${VARYING_DEF}"
            COMMENT "Compiling ${SHADER_NAME} vertex shader for ${SHADER_PROFILE}"
            VERBATIM
        )
        list(APPEND GENERATED_HEADERS "${SHADER_HEADER}")
    endforeach()

    set(SHADER_TARGET "${TARGET}_${SHADER_NAME}_shaders")
    add_custom_target("${SHADER_TARGET}" DEPENDS ${GENERATED_HEADERS})
    add_dependencies("${TARGET}" "${SHADER_TARGET}")
    set_source_files_properties(${GENERATED_HEADERS} PROPERTIES GENERATED TRUE)
    target_sources("${TARGET}" PRIVATE
        "${VERTEX_SHADER}"
        "${VARYING_DEF}"
        ${GENERATED_HEADERS}
    )
    target_include_directories("${TARGET}" PRIVATE "${SHADER_OUTPUT_DIR}")
endfunction()
