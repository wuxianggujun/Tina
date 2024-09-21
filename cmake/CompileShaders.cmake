cmake_minimum_required(VERSION 3.20)

function(add_shader_compile_dir SHADER_DIR)

    if (CMAKE_VERSION VERSION_LESS "3.20")
        message(FATAL_ERROR "CMake version 3.20 or greater is required to use add_shader_compile_dir")
        return()
    endif ()

    if (NOT EXISTS "${SHADER_DIR}")
        message(NOTICE "Shader directory ${SHADER_DIR} does not exist")
        return()
    endif ()

    if (CMAKE_CROSSCOMPILING AND NOT ("${CMAKE_HOST_SYSTEM_NAME}" STREQUAL "${CMAKE_SYSTEM_NAME}"))
        message(STATUS "Not compiling shaders during cross-compilation")
        return()
    endif ()

    set(SHADER_OUTPUT_DIR "${CMAKE_BINARY_DIR}/resources/shaders")
    file(MAKE_DIRECTORY "${SHADER_OUTPUT_DIR}")

    set(BGFX_SHADER_INCLUDE_PATH "${BGFX_DIR}/src")

    set(BGFX_SHADERC bgfx::shaderc)

    set(PROFILES 120 300_es spirv) # pssl
    if (UNIX AND NOT APPLE)
        set(PLATFORM LINUX)
    elseif (EMSCRIPTEN)
        set(PLATFORM ASM_JS)
    elseif (APPLE)
        set(PLATFORM OSX)
        list(APPEND PROFILES metal)
    elseif (
            WIN32
            OR MINGW
            OR MSYS
            OR CYGWIN)
        set(PLATFORM WINDOWS)
        list(APPEND PROFILES s_4_0)
        list(APPEND PROFILES s_5_0)
    else ()
        message(error "shaderc: Unsupported platform")
    endif ()

    #set(OUTPUTS "")
    foreach (PROFILE ${PROFILES})
        _bgfx_get_profile_ext(${PROFILE} PROFILE_EXT)
        set(PROFILE_OUTPUT_DIR "${SHADER_OUTPUT_DIR}/${PROFILE_EXT}")
        file(MAKE_DIRECTORY "${PROFILE_OUTPUT_DIR}")

        foreach (SHADER_NAME ${ARGN})
            set(VERTEX_SHADER_FILE "${SHADER_DIR}/${SHADER_NAME}.vs.sc")
            set(FRAGMENT_SHADER_FILE "${SHADER_DIR}/${SHADER_NAME}.fs.sc")
            set(VARYING_DEF_FILE "${SHADER_DIR}/${SHADER_NAME}.def.sc")

            set(OUTPUT_VERTEX "${PROFILE_OUTPUT_DIR}/${SHADER_NAME}.vs.bin")
            set(OUTPUT_FRAGMENT "${PROFILE_OUTPUT_DIR}/${SHADER_NAME}.fs.bin")
            
            if (NOT EXISTS "${VARYING_DEF_FILE}")
                set(VARYING_DEF_FILE "${SHADER_DIR}/varying.def.sc")
                message(STATUS "Vertex shader file not found, using default: ${VARYING_DEF_FILE}")
            endif ()


            add_custom_command(
                    OUTPUT ${OUTPUT_VERTEX}
                    DEPENDS ${VERTEX_SHADER_FILE} ${VARYING_DEF_FILE}
                    COMMAND ${BGFX_SHADERC} --type vertex --platform ${PLATFORM} --profile ${PROFILE} -f ${VERTEX_SHADER_FILE} --varyingdef ${VARYING_DEF_FILE} -o ${OUTPUT_VERTEX} -i ${SHADER_DIR} -i ${BGFX_SHADER_INCLUDE_PATH}
                    COMMENT "Compiling vertex shader ${SHADER_NAME} for ${PROFILE_EXT} profile"
            )

            add_custom_command(
                    OUTPUT ${OUTPUT_FRAGMENT}
                    DEPENDS ${FRAGMENT_SHADER_FILE} ${VARYING_DEF_FILE}
                    COMMAND ${BGFX_SHADERC} --type fragment --platform ${PLATFORM} --profile ${PROFILE} -f ${FRAGMENT_SHADER_FILE} --varyingdef ${VARYING_DEF_FILE} -o ${OUTPUT_FRAGMENT} -i ${SHADER_DIR} -i ${BGFX_SHADER_INCLUDE_PATH}
                    COMMENT "Compiling fragment shader ${SHADER_NAME} for ${PROFILE_EXT} profile"
            )

            list(APPEND SHADER_SOURCE_LIST ${VERTEX_SHADER_FILE} ${FRAGMENT_SHADER_FILE} ${VARYING_DEF_FILE})
            list(APPEND SHADER_OUTPUT_LIST ${OUTPUT_VERTEX} ${OUTPUT_FRAGMENT})
        endforeach ()
    endforeach ()

    add_custom_target(shaders ALL DEPENDS ${SHADER_OUTPUT_LIST})
    source_group(TREE "${SHADER_DIR}" PREFIX "Shader Files" FILES ${SHADER_SOURCE_LIST})
    target_sources(shaders PRIVATE ${SHADER_SOURCE_LIST})

endfunction()