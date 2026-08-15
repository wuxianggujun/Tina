include_guard(GLOBAL)

function(tina_add_bgfx_embedded_shader TARGET SHADER_NAME VERTEX_SHADER FRAGMENT_SHADER VARYING_DEF)
    if(NOT TARGET "${TARGET}")
        message(FATAL_ERROR "tina_add_bgfx_embedded_shader: target '${TARGET}' does not exist")
    endif()
    if(NOT TARGET bgfx::shaderc)
        message(FATAL_ERROR
            "tina_add_bgfx_embedded_shader: bgfx::shaderc is required for the private bgfx backend")
    endif()

    set(SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/shaders/${SHADER_NAME}")
    set(SHADER_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/thirdparty/bgfx.cmake/bgfx/src")

    set(SHADER_PROFILES
        "glsl|linux|120"
        "spv|linux|spirv"
    )
    if(WIN32)
        list(APPEND SHADER_PROFILES "dxbc|windows|s_5_0")
    endif()

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
                COMMAND $<TARGET_FILE:bgfx::shaderc>
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
                    bgfx::shaderc
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
    if(NOT TARGET bgfx::shaderc)
        message(FATAL_ERROR
            "tina_add_bgfx_embedded_vertex_shader: bgfx::shaderc is required for the private bgfx backend")
    endif()

    set(SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/shaders/${SHADER_NAME}")
    set(SHADER_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/thirdparty/bgfx.cmake/bgfx/src")

    set(SHADER_PROFILES
        "glsl|linux|120"
        "spv|linux|spirv"
    )
    if(WIN32)
        list(APPEND SHADER_PROFILES "dxbc|windows|s_5_0")
    endif()

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
            COMMAND $<TARGET_FILE:bgfx::shaderc>
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
                bgfx::shaderc
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
