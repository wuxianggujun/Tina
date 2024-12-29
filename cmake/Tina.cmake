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

    option(COMPILE_SHADERS_TO_HEADERS "Compile shaders to C++ header files" OFF)

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

    foreach (PROFILE ${PROFILES})
        _bgfx_get_profile_ext(${PROFILE} PROFILE_EXT)
        set(PROFILE_OUTPUT_DIR "${SHADER_OUTPUT_DIR}/${PROFILE_EXT}")
        file(MAKE_DIRECTORY "${PROFILE_OUTPUT_DIR}")

        foreach (SHADER_NAME ${ARGN})
            set(VERTEX_SHADER_FILE "${SHADER_DIR}/${SHADER_NAME}.vs.sc")
            set(FRAGMENT_SHADER_FILE "${SHADER_DIR}/${SHADER_NAME}.fs.sc")
            set(VARYING_DEF_FILE "${SHADER_DIR}/${SHADER_NAME}.def.sc")

            set(OUTPUT_VERTEX "${PROFILE_OUTPUT_DIR}/${SHADER_NAME}.vs")
            set(OUTPUT_FRAGMENT "${PROFILE_OUTPUT_DIR}/${SHADER_NAME}.fs")

            if (NOT EXISTS "${VARYING_DEF_FILE}")
                set(VARYING_DEF_FILE "${SHADER_DIR}/varying.def.sc")
                message(STATUS "Vertex shader file not found, using default: ${VARYING_DEF_FILE}")
            endif ()

            if (COMPILE_SHADERS_TO_HEADERS)
                set(OUTPUT_VERTEX "${OUTPUT_VERTEX}.h")
                set(OUTPUT_FRAGMENT "${OUTPUT_FRAGMENT}.h")
            else()
                set(OUTPUT_VERTEX "${OUTPUT_VERTEX}.bin")
                set(OUTPUT_FRAGMENT "${OUTPUT_FRAGMENT}.bin")
            endif()

            set(BIN2C_PART "")
            if(COMPILE_SHADERS_TO_HEADERS)
                set(BIN2C_PART "--bin2c ${SHADER_NAME}_${PROFILE_EXT}")
            endif()

            _bgfx_shaderc_parse(
                    CLI_VERTEX
                    ${BIN2C_PART}
                    VERTEX ${PLATFORM} WERROR "$<$<CONFIG:debug>:DEBUG>$<$<CONFIG:relwithdebinfo>:DEBUG>"
                    FILE ${VERTEX_SHADER_FILE}
                    OUTPUT ${OUTPUT_VERTEX}
                    PROFILE ${PROFILE}
                    O "$<$<CONFIG:debug>:0>$<$<CONFIG:release>:3>$<$<CONFIG:relwithdebinfo>:3>$<$<CONFIG:minsizerel>:3>"
                    VARYINGDEF ${VARYING_DEF_FILE}
                    INCLUDES ${BGFX_SHADER_INCLUDE_PATH} ${SHADER_DIR}
            )

            _bgfx_shaderc_parse(
                    CLI_FRAGMENT
                    ${BIN2C_PART}
                    FRAGMENT ${PLATFORM} WERROR "$<$<CONFIG:debug>:DEBUG>$<$<CONFIG:relwithdebinfo>:DEBUG>"
                    FILE ${FRAGMENT_SHADER_FILE}
                    OUTPUT ${OUTPUT_FRAGMENT}
                    PROFILE ${PROFILE}
                    O "$<$<CONFIG:debug>:0>$<$<CONFIG:release>:3>$<$<CONFIG:relwithdebinfo>:3>$<$<CONFIG:minsizerel>:3>"
                    VARYINGDEF ${VARYING_DEF_FILE}
                    INCLUDES ${BGFX_SHADER_INCLUDE_PATH} ${SHADER_DIR}
            )

            add_custom_command(
                    OUTPUT ${OUTPUT_VERTEX}
                    DEPENDS ${VERTEX_SHADER_FILE} ${VARYING_DEF_FILE}
                    COMMAND ${BGFX_SHADERC} ${CLI_VERTEX}
                    COMMENT "Compiling vertex shader ${SHADER_NAME} for ${PROFILE_EXT} profile"
            )

            add_custom_command(
                    OUTPUT ${OUTPUT_FRAGMENT}
                    DEPENDS ${FRAGMENT_SHADER_FILE} ${VARYING_DEF_FILE}
                    COMMAND ${BGFX_SHADERC} ${CLI_FRAGMENT}
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