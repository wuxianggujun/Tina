file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/shaders")

function(compile_shaders)
    if(UNIX AND NOT APPLE)
        set(platform linux)
        set(PROFILES 120 spirv)
    else()
        set(platform windows)
        set(PROFILES 120 spirv s_5_0)
    endif()

    # 设置通用的varying.def.sc文件
    set(COMMON_VARYING_DEF "${CMAKE_SOURCE_DIR}/resources/shaders/varying.def.sc")

    foreach(profile IN LISTS PROFILES)
        foreach(shader_name IN LISTS ARGN)
            set(IN_VERTEX    "${CMAKE_SOURCE_DIR}/resources/shaders/${shader_name}.vs.sc")
            set(IN_FRAGMENT  "${CMAKE_SOURCE_DIR}/resources/shaders/${shader_name}.fs.sc")

            set(OUT_VERTEX   "${CMAKE_BINARY_DIR}/shaders/${shader_name}.${profile}.vs.bin")
            set(OUT_FRAGMENT "${CMAKE_BINARY_DIR}/shaders/${shader_name}.${profile}.fs.bin")

            set(BGFX_SHADER_INCLUDE_PATH "${CMAKE_SOURCE_DIR}/dependencies/bgfx.cmake/bgfx/src")

            set(BGFX_SHADERC bgfx::shaderc)

            add_custom_command(
                    OUTPUT ${OUT_VERTEX}
                    DEPENDS ${IN_VERTEX} ${COMMON_VARYING_DEF}
                    COMMAND ${BGFX_SHADERC} --type vertex --platform ${platform} --profile ${profile} -f ${IN_VERTEX} --varyingdef ${COMMON_VARYING_DEF} -o ${OUT_VERTEX} -i "${BGFX_SHADER_INCLUDE_PATH}"
            )

            add_custom_command(
                    OUTPUT ${OUT_FRAGMENT}
                    DEPENDS ${IN_FRAGMENT} ${COMMON_VARYING_DEF}
                    COMMAND ${BGFX_SHADERC} --type fragment --platform ${platform} --profile ${profile} -f ${IN_FRAGMENT} --varyingdef ${COMMON_VARYING_DEF} -o ${OUT_FRAGMENT} -i "${BGFX_SHADER_INCLUDE_PATH}"
            )

            # Create output directories for each renderer type
            foreach(renderer_type IN ITEMS dx11 glsl spv metal)
                file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/shaders/${renderer_type}")
            endforeach()

            # Copy compiled shaders to appropriate renderer directories
            if(profile STREQUAL "s_5_0")
                add_custom_command(
                    TARGET shaders POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy ${OUT_VERTEX} "${CMAKE_BINARY_DIR}/shaders/dx11/${shader_name}.vs.bin"
                    COMMAND ${CMAKE_COMMAND} -E copy ${OUT_FRAGMENT} "${CMAKE_BINARY_DIR}/shaders/dx11/${shader_name}.fs.bin"
                )
            elseif(profile STREQUAL "120")
                add_custom_command(
                    TARGET shaders POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy ${OUT_VERTEX} "${CMAKE_BINARY_DIR}/shaders/glsl/${shader_name}.vs.bin"
                    COMMAND ${CMAKE_COMMAND} -E copy ${OUT_FRAGMENT} "${CMAKE_BINARY_DIR}/shaders/glsl/${shader_name}.fs.bin"
                )
            elseif(profile STREQUAL "spirv")
                add_custom_command(
                    TARGET shaders POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy ${OUT_VERTEX} "${CMAKE_BINARY_DIR}/shaders/spv/${shader_name}.vs.bin"
                    COMMAND ${CMAKE_COMMAND} -E copy ${OUT_FRAGMENT} "${CMAKE_BINARY_DIR}/shaders/spv/${shader_name}.fs.bin"
                )
            endif()

            list(APPEND SHADER_SOURCE_LIST ${IN_VERTEX} ${IN_FRAGMENT} ${COMMON_VARYING_DEF})
            list(APPEND SHADER_OUTPUT_LIST ${OUT_VERTEX} ${OUT_FRAGMENT})
        endforeach()
    endforeach()

    add_custom_target(shaders DEPENDS ${SHADER_OUTPUT_LIST})

    source_group(TREE "${CMAKE_SOURCE_DIR}/resources/shaders/" PREFIX "Shader Files" FILES ${SHADER_SOURCE_LIST})
    target_sources(shaders PRIVATE ${SHADER_SOURCE_LIST})
endfunction()

compile_shaders(
        sprite
        gui
        bump
        cubes
)
