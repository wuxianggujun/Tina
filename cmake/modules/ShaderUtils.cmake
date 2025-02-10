 function(add_shader_compile_dir SHADER_DIR)
    if (NOT EXISTS "${SHADER_DIR}")
        message(NOTICE "Shader directory ${SHADER_DIR} does not exist")
        return()
    endif ()

    # 设置输出目录
    set(SHADER_OUTPUT_DIR "${CMAKE_BINARY_DIR}/bin/shaders")
    file(MAKE_DIRECTORY "${SHADER_OUTPUT_DIR}")

    # 设置 bgfx 相关路径
    set(BGFX_SHADER_INCLUDE_PATH "${BGFX_DIR}/src")
    set(BGFX_SHADERC bgfx::shaderc)

    # 检查varying定义文件
    set(VARYING_DEF_FILE "${SHADER_DIR}/varying.def.sc")
    if (NOT EXISTS "${VARYING_DEF_FILE}")
        message(WARNING "Varying definition file does not exist: ${VARYING_DEF_FILE}")
        return()
    endif()

    set(ALL_SHADERS "")
    set(SHADER_NAMES ${ARGN})

    # 处理每个着色器名称
    foreach(SHADER_NAME ${SHADER_NAMES})
        set(VERTEX_SHADER "${SHADER_DIR}/vs_${SHADER_NAME}.sc")
        set(FRAGMENT_SHADER "${SHADER_DIR}/fs_${SHADER_NAME}.sc")

        if(NOT EXISTS "${VERTEX_SHADER}")
            message(WARNING "Vertex shader does not exist: ${VERTEX_SHADER}")
            continue()
        endif()

        # DirectX 11
        set(OUTPUT_DIR_DX11 "${SHADER_OUTPUT_DIR}/dx11")
        file(MAKE_DIRECTORY "${OUTPUT_DIR_DX11}")
        set(OUTPUT_VERTEX_DX11 "${OUTPUT_DIR_DX11}/${SHADER_NAME}.vs.bin")
        add_custom_command(
            OUTPUT ${OUTPUT_VERTEX_DX11}
            COMMAND ${BGFX_SHADERC}
            ARGS -f "${VERTEX_SHADER}"
                 -o "${OUTPUT_VERTEX_DX11}"
                 -i "${BGFX_SHADER_INCLUDE_PATH}"
                 --platform windows
                 --type vertex
                 --profile s_5_0
                 -v "${VARYING_DEF_FILE}"
            DEPENDS "${VERTEX_SHADER}" "${VARYING_DEF_FILE}"
            COMMENT "Compiling vertex shader ${SHADER_NAME} for DirectX 11"
        )
        list(APPEND ALL_SHADERS ${OUTPUT_VERTEX_DX11})

        if(EXISTS "${FRAGMENT_SHADER}")
            set(OUTPUT_FRAGMENT_DX11 "${OUTPUT_DIR_DX11}/${SHADER_NAME}.fs.bin")
            add_custom_command(
                OUTPUT ${OUTPUT_FRAGMENT_DX11}
                COMMAND ${BGFX_SHADERC}
                ARGS -f "${FRAGMENT_SHADER}"
                     -o "${OUTPUT_FRAGMENT_DX11}"
                     -i "${BGFX_SHADER_INCLUDE_PATH}"
                     --platform windows
                     --type fragment
                     --profile s_5_0
                     -v "${VARYING_DEF_FILE}"
                DEPENDS "${FRAGMENT_SHADER}" "${VARYING_DEF_FILE}"
                COMMENT "Compiling fragment shader ${SHADER_NAME} for DirectX 11"
            )
            list(APPEND ALL_SHADERS ${OUTPUT_FRAGMENT_DX11})
        endif()

        # DirectX 9
        set(OUTPUT_DIR_DX9 "${SHADER_OUTPUT_DIR}/dx9")
        file(MAKE_DIRECTORY "${OUTPUT_DIR_DX9}")
        set(OUTPUT_VERTEX_DX9 "${OUTPUT_DIR_DX9}/${SHADER_NAME}.vs.bin")
        add_custom_command(
            OUTPUT ${OUTPUT_VERTEX_DX9}
            COMMAND ${BGFX_SHADERC}
            ARGS -f "${VERTEX_SHADER}"
                 -o "${OUTPUT_VERTEX_DX9}"
                 -i "${BGFX_SHADER_INCLUDE_PATH}"
                 --platform windows
                 --type vertex
                 --profile s_4_0
                 -v "${VARYING_DEF_FILE}"
            DEPENDS "${VERTEX_SHADER}" "${VARYING_DEF_FILE}"
            COMMENT "Compiling vertex shader ${SHADER_NAME} for DirectX 9"
        )
        list(APPEND ALL_SHADERS ${OUTPUT_VERTEX_DX9})

        if(EXISTS "${FRAGMENT_SHADER}")
            set(OUTPUT_FRAGMENT_DX9 "${OUTPUT_DIR_DX9}/${SHADER_NAME}.fs.bin")
            add_custom_command(
                OUTPUT ${OUTPUT_FRAGMENT_DX9}
                COMMAND ${BGFX_SHADERC}
                ARGS -f "${FRAGMENT_SHADER}"
                     -o "${OUTPUT_FRAGMENT_DX9}"
                     -i "${BGFX_SHADER_INCLUDE_PATH}"
                     --platform windows
                     --type fragment
                     --profile s_4_0
                     -v "${VARYING_DEF_FILE}"
                DEPENDS "${FRAGMENT_SHADER}" "${VARYING_DEF_FILE}"
                COMMENT "Compiling fragment shader ${SHADER_NAME} for DirectX 9"
            )
            list(APPEND ALL_SHADERS ${OUTPUT_FRAGMENT_DX9})
        endif()

        # OpenGL/Vulkan (SPIR-V)
        set(OUTPUT_DIR_SPIRV "${SHADER_OUTPUT_DIR}/spirv")
        file(MAKE_DIRECTORY "${OUTPUT_DIR_SPIRV}")
        set(OUTPUT_VERTEX_SPIRV "${OUTPUT_DIR_SPIRV}/${SHADER_NAME}.vs.bin")
        add_custom_command(
            OUTPUT ${OUTPUT_VERTEX_SPIRV}
            COMMAND ${BGFX_SHADERC}
            ARGS -f "${VERTEX_SHADER}"
                 -o "${OUTPUT_VERTEX_SPIRV}"
                 -i "${BGFX_SHADER_INCLUDE_PATH}"
                 --platform linux
                 --type vertex
                 --profile 120
                 -v "${VARYING_DEF_FILE}"
            DEPENDS "${VERTEX_SHADER}" "${VARYING_DEF_FILE}"
            COMMENT "Compiling vertex shader ${SHADER_NAME} for OpenGL/Vulkan"
        )
        list(APPEND ALL_SHADERS ${OUTPUT_VERTEX_SPIRV})

        if(EXISTS "${FRAGMENT_SHADER}")
            set(OUTPUT_FRAGMENT_SPIRV "${OUTPUT_DIR_SPIRV}/${SHADER_NAME}.fs.bin")
            add_custom_command(
                OUTPUT ${OUTPUT_FRAGMENT_SPIRV}
                COMMAND ${BGFX_SHADERC}
                ARGS -f "${FRAGMENT_SHADER}"
                     -o "${OUTPUT_FRAGMENT_SPIRV}"
                     -i "${BGFX_SHADER_INCLUDE_PATH}"
                     --platform linux
                     --type fragment
                     --profile 120
                     -v "${VARYING_DEF_FILE}"
                DEPENDS "${FRAGMENT_SHADER}" "${VARYING_DEF_FILE}"
                COMMENT "Compiling fragment shader ${SHADER_NAME} for OpenGL/Vulkan"
            )
            list(APPEND ALL_SHADERS ${OUTPUT_FRAGMENT_SPIRV})
        endif()
    endforeach()

    if(ALL_SHADERS)
        add_custom_target(shaders ALL
            DEPENDS ${ALL_SHADERS}
            COMMENT "Compiling all shaders"
        )
    endif()
endfunction()
