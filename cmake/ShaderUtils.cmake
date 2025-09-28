cmake_minimum_required(VERSION 3.20)

# 获取bgfx平台标识符
function(_bgfx_get_platform PLATFORM_OUT)
    if(WIN32 OR MINGW OR MSYS OR CYGWIN)
        set(PLATFORM "windows")
    elseif(APPLE)
        if(IOS)
            set(PLATFORM "ios")
        else()
            set(PLATFORM "osx")
        endif()
    elseif(ANDROID)
        set(PLATFORM "android")
    elseif(EMSCRIPTEN)
        set(PLATFORM "asm.js")
    elseif(UNIX AND NOT APPLE)
        set(PLATFORM "linux")
    else()
        message(FATAL_ERROR "Unsupported platform for shader compilation")
    endif()
    
    set(${PLATFORM_OUT} ${PLATFORM} PARENT_SCOPE)
endfunction()

# 根据平台获取支持的着色器profiles
function(_bgfx_get_shader_profiles PROFILES_OUT)
    set(PROFILES "120;300_es;spirv")
    
    if(IOS)
        list(APPEND PROFILES "metal")
    elseif(APPLE)
        list(APPEND PROFILES "metal")
    elseif(WIN32 OR MINGW OR MSYS OR CYGWIN)
        list(APPEND PROFILES "s_4_0;s_5_0")
    endif()
    
    set(${PROFILES_OUT} ${PROFILES} PARENT_SCOPE)
endfunction()

# profile文件扩展名映射（与embedded_shader.h一致）
function(_bgfx_get_profile_ext PROFILE PROFILE_EXT_OUT)
    string(REPLACE "300_es" "essl" PROFILE_EXT ${PROFILE})
    string(REPLACE "120" "glsl" PROFILE_EXT ${PROFILE_EXT})
    string(REPLACE "spirv" "spv" PROFILE_EXT ${PROFILE_EXT})
    string(REPLACE "metal" "mtl" PROFILE_EXT ${PROFILE_EXT})
    string(REPLACE "s_4_0" "dx10" PROFILE_EXT ${PROFILE_EXT})
    string(REPLACE "s_5_0" "dx11" PROFILE_EXT ${PROFILE_EXT})
    set(${PROFILE_EXT_OUT} ${PROFILE_EXT} PARENT_SCOPE)
endfunction()

# profile路径扩展名映射
function(_bgfx_get_profile_path_ext PROFILE PROFILE_PATH_EXT_OUT)
    string(REPLACE "300_es" "essl" PROFILE_PATH_EXT ${PROFILE})
    string(REPLACE "120" "glsl" PROFILE_PATH_EXT ${PROFILE_PATH_EXT})
    string(REPLACE "s_4_0" "dx10" PROFILE_PATH_EXT ${PROFILE_PATH_EXT})
    string(REPLACE "s_5_0" "dx11" PROFILE_PATH_EXT ${PROFILE_PATH_EXT})
    set(${PROFILE_PATH_EXT_OUT} ${PROFILE_PATH_EXT} PARENT_SCOPE)
endfunction()

# 编译着色器目录
# add_shader_compile_dir(SHADER_DIR [GENERATE_HEADERS] [DEBUG_SHADERS] shader_names...)
# GENERATE_HEADERS: 生成 .hpp 头文件
# DEBUG_SHADERS: 生成 .hlsl 调试文件（默认关闭）
function(add_shader_compile_dir SHADER_DIR)
    cmake_parse_arguments(ARGS "GENERATE_HEADERS;DEBUG_SHADERS" "" "" ${ARGN})
    
    if(NOT EXISTS "${SHADER_DIR}")
        message(FATAL_ERROR "Shader directory ${SHADER_DIR} does not exist")
        return()
    endif()

    # 检查shaderc是否可用
    if(NOT TARGET bgfx::shaderc)
        message(FATAL_ERROR "bgfx::shaderc target not found. Cannot compile shaders.")
        return()
    endif()

    # 设置输出目录（与运行期 ResourceCache 约定保持一致）
    # 最终运行时从 CoreData/Shaders/BGFX 下按 profile 读取：glsl/essl/dx10/dx11/spirv/metal
    set(SHADER_OUTPUT_DIR "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/CoreData/Shaders/BGFX")
    file(MAKE_DIRECTORY "${SHADER_OUTPUT_DIR}")
    message(STATUS "Shader output directory: ${SHADER_OUTPUT_DIR}")

    # 设置 bgfx 相关路径
    set(BGFX_SHADER_INCLUDE_PATH "${BGFX_DIR}/src")
    if(NOT EXISTS "${BGFX_SHADER_INCLUDE_PATH}")
        message(FATAL_ERROR "BGFX shader include path does not exist: ${BGFX_SHADER_INCLUDE_PATH}")
        return()
    endif()

    # 获取平台相关配置
    _bgfx_get_platform(PLATFORM)
    _bgfx_get_shader_profiles(PROFILES)
    message(STATUS "Compiling shaders for platform: ${PLATFORM}")
    message(STATUS "Using shader profiles: ${PROFILES}")

    set(ALL_SHADER_OUTPUTS "")
    set(ALL_SHADER_SOURCES "")

    # 获取着色器名称列表（移除GENERATE_HEADERS参数）
    set(SHADER_NAMES ${ARGS_UNPARSED_ARGUMENTS})

    # 处理每个着色器
    foreach(SHADER_NAME ${SHADER_NAMES})
        set(VERTEX_SHADER "${SHADER_DIR}/${SHADER_NAME}_vs.sc")
        set(FRAGMENT_SHADER "${SHADER_DIR}/${SHADER_NAME}_fs.sc")
        
        # 首先检查特定的def文件，如果不存在则使用默认的varying.def.sc
        set(SPECIFIC_DEF_FILE "${SHADER_DIR}/${SHADER_NAME}.def.sc")
        set(DEFAULT_DEF_FILE "${SHADER_DIR}/varying.def.sc")
        
        if(EXISTS "${SPECIFIC_DEF_FILE}")
            set(VARYING_DEF_FILE "${SPECIFIC_DEF_FILE}")
            message(STATUS "Using shader-specific def file: ${SPECIFIC_DEF_FILE}")
        elseif(EXISTS "${DEFAULT_DEF_FILE}")
            set(VARYING_DEF_FILE "${DEFAULT_DEF_FILE}")
            message(STATUS "Using default def file: ${DEFAULT_DEF_FILE}")
        else()
            message(FATAL_ERROR "No varying definition file found for shader ${SHADER_NAME}")
            continue()
        endif()

        if(NOT EXISTS "${VERTEX_SHADER}")
            message(FATAL_ERROR "Vertex shader not found: ${VERTEX_SHADER}")
            continue()
        endif()

        if(NOT EXISTS "${FRAGMENT_SHADER}")
            message(FATAL_ERROR "Fragment shader not found: ${FRAGMENT_SHADER}")
            continue()
        endif()

        list(APPEND ALL_SHADER_SOURCES 
            "${VERTEX_SHADER}" 
            "${FRAGMENT_SHADER}" 
            "${VARYING_DEF_FILE}"
        )

        # 为每个profile编译着色器
        foreach(PROFILE ${PROFILES})
            _bgfx_get_profile_path_ext(${PROFILE} PROFILE_PATH_EXT)
            
            set(PROFILE_OUTPUT_DIR "${SHADER_OUTPUT_DIR}/${PROFILE_PATH_EXT}")
            file(MAKE_DIRECTORY "${PROFILE_OUTPUT_DIR}")

            # 动态设置平台，SPIRV需要使用linux平台
            set(SHADER_PLATFORM ${PLATFORM})
            if(PROFILE STREQUAL "spirv")
                set(SHADER_PLATFORM "linux")
            endif()

            # 顶点着色器
            set(VERTEX_OUTPUT "${PROFILE_OUTPUT_DIR}/${SHADER_NAME}_vs.bin")
            set(VERTEX_HEADER "${PROFILE_OUTPUT_DIR}/${SHADER_NAME}_vs.hpp")

            # 基本的编译命令（始终生成二进制文件）
            add_custom_command(
                OUTPUT ${VERTEX_OUTPUT}
                COMMAND $<TARGET_FILE:bgfx::shaderc>
                    --type vertex
                    --platform ${SHADER_PLATFORM}
                    --profile ${PROFILE}
                    -f "${VERTEX_SHADER}"
                    -o "${VERTEX_OUTPUT}"
                    -i "${BGFX_SHADER_INCLUDE_PATH}"
                    -i "${SHADER_DIR}"
                    --varyingdef "${VARYING_DEF_FILE}"
                    -O "$<$<CONFIG:debug>:0>$<$<CONFIG:release>:3>$<$<CONFIG:relwithdebinfo>:3>$<$<CONFIG:minsizerel>:3>"
                    "$<$<BOOL:${ARGS_DEBUG_SHADERS}>:--debug>"
                    --Werror
                DEPENDS "${VERTEX_SHADER}" "${VARYING_DEF_FILE}"
                COMMENT "Compiling vertex shader ${SHADER_NAME} for ${PROFILE}"
                VERBATIM
            )

            list(APPEND ALL_SHADER_OUTPUTS "${VERTEX_OUTPUT}")

            # 如果需要生成头文件
            if(ARGS_GENERATE_HEADERS)
                _bgfx_get_profile_ext(${PROFILE} PROFILE_EXT)
                add_custom_command(
                    OUTPUT ${VERTEX_HEADER}
                    COMMAND $<TARGET_FILE:bgfx::shaderc>
                        --type vertex
                        --platform ${SHADER_PLATFORM}
                        --profile ${PROFILE}
                        -f "${VERTEX_SHADER}"
                        -o "${VERTEX_HEADER}"
                        -i "${BGFX_SHADER_INCLUDE_PATH}"
                        -i "${SHADER_DIR}"
                        --varyingdef "${VARYING_DEF_FILE}"
                        --bin2c "${SHADER_NAME}_vs_${PROFILE_EXT}"
                        -O "$<$<CONFIG:debug>:0>$<$<CONFIG:release>:3>$<$<CONFIG:relwithdebinfo>:3>$<$<CONFIG:minsizerel>:3>"
                        "$<$<CONFIG:debug>:--debug>"
                        --Werror
                    DEPENDS "${VERTEX_SHADER}" "${VARYING_DEF_FILE}"
                    COMMENT "Generating vertex shader header ${SHADER_NAME} for ${PROFILE}"
                    VERBATIM
                )
                list(APPEND ALL_SHADER_OUTPUTS "${VERTEX_HEADER}")
            endif()

            # 片段着色器
            set(FRAGMENT_OUTPUT "${PROFILE_OUTPUT_DIR}/${SHADER_NAME}_fs.bin")
            set(FRAGMENT_HEADER "${PROFILE_OUTPUT_DIR}/${SHADER_NAME}_fs.hpp")

            # 基本的编译命令（始终生成二进制文件）
            add_custom_command(
                OUTPUT ${FRAGMENT_OUTPUT}
                COMMAND $<TARGET_FILE:bgfx::shaderc>
                    --type fragment
                    --platform ${SHADER_PLATFORM}
                    --profile ${PROFILE}
                    -f "${FRAGMENT_SHADER}"
                    -o "${FRAGMENT_OUTPUT}"
                    -i "${BGFX_SHADER_INCLUDE_PATH}"
                    -i "${SHADER_DIR}"
                    --varyingdef "${VARYING_DEF_FILE}"
                    -O "$<$<CONFIG:debug>:0>$<$<CONFIG:release>:3>$<$<CONFIG:relwithdebinfo>:3>$<$<CONFIG:minsizerel>:3>"
                    "$<$<BOOL:${ARGS_DEBUG_SHADERS}>:--debug>"
                    --Werror
                DEPENDS "${FRAGMENT_SHADER}" "${VARYING_DEF_FILE}"
                COMMENT "Compiling fragment shader ${SHADER_NAME} for ${PROFILE}"
                VERBATIM
            )

            list(APPEND ALL_SHADER_OUTPUTS "${FRAGMENT_OUTPUT}")

            # 如果需要生成头文件
            if(ARGS_GENERATE_HEADERS)
                add_custom_command(
                    OUTPUT ${FRAGMENT_HEADER}
                    COMMAND $<TARGET_FILE:bgfx::shaderc>
                        --type fragment
                        --platform ${SHADER_PLATFORM}
                        --profile ${PROFILE}
                        -f "${FRAGMENT_SHADER}"
                        -o "${FRAGMENT_HEADER}"
                        -i "${BGFX_SHADER_INCLUDE_PATH}"
                        -i "${SHADER_DIR}"
                        --varyingdef "${VARYING_DEF_FILE}"
                        --bin2c "${SHADER_NAME}_fs_${PROFILE_EXT}"
                        -O "$<$<CONFIG:debug>:0>$<$<CONFIG:release>:3>$<$<CONFIG:relwithdebinfo>:3>$<$<CONFIG:minsizerel>:3>"
                        "$<$<CONFIG:debug>:--debug>"
                        --Werror
                    DEPENDS "${FRAGMENT_SHADER}" "${VARYING_DEF_FILE}"
                    COMMENT "Generating fragment shader header ${SHADER_NAME} for ${PROFILE}"
                    VERBATIM
                )
                list(APPEND ALL_SHADER_OUTPUTS "${FRAGMENT_HEADER}")
            endif()
        endforeach()
    endforeach()

    if(ALL_SHADER_OUTPUTS)
        # 创建着色器编译目标
        add_custom_target(shaders ALL
            DEPENDS ${ALL_SHADER_OUTPUTS}
            COMMENT "Compiling all shaders"
        )
        
        # 添加源文件以在IDE中显示
        target_sources(shaders PRIVATE ${ALL_SHADER_SOURCES})
        source_group(TREE "${SHADER_DIR}" PREFIX "Shader Files" FILES ${ALL_SHADER_SOURCES})

        # 确保shaderc在编译着色器之前已经构建
        add_dependencies(shaders bgfx::shaderc)

        # 兼容示例工程中的依赖名称：
        # 示例使用了一个名为 tina2d_bgfx_shaders 的自定义目标作为依赖，
        # 这里为实际的 shaders 目标添加一个别名目标，避免依赖名不一致导致未触发编译。
        if (NOT TARGET tina2d_bgfx_shaders)
            add_custom_target(tina2d_bgfx_shaders)
            add_dependencies(tina2d_bgfx_shaders shaders)
        endif ()

        message(STATUS "Created shader target with outputs:")
        foreach(OUTPUT ${ALL_SHADER_OUTPUTS})
            message(STATUS "  ${OUTPUT}")
        endforeach()
    else()
        message(FATAL_ERROR "No shader outputs were configured")
    endif()
endfunction()

# 标准化的bgfx着色器编译函数（基于bgfx.cmake）
# bgfx_compile_shaders(
#     TYPE VERTEX|FRAGMENT|COMPUTE
#     SHADERS filenames
#     VARYING_DEF filename
#     OUTPUT_DIR directory
#     OUT_FILES_VAR variable name
#     INCLUDE_DIRS directories
#     [AS_HEADERS]
#     [DEBUG_SHADERS]  # 启用时生成 .hlsl 调试文件
# )
function(bgfx_compile_shaders)
    set(options AS_HEADERS DEBUG_SHADERS)
    set(oneValueArgs TYPE VARYING_DEF OUTPUT_DIR OUT_FILES_VAR)
    set(multiValueArgs SHADERS INCLUDE_DIRS)
    cmake_parse_arguments(ARGS "${options}" "${oneValueArgs}" "${multiValueArgs}" "${ARGN}")

    if(NOT ARGS_TYPE OR NOT ARGS_SHADERS OR NOT ARGS_VARYING_DEF OR NOT ARGS_OUTPUT_DIR)
        message(FATAL_ERROR "bgfx_compile_shaders: Missing required arguments")
        return()
    endif()

    # 检查shaderc是否可用
    if(NOT TARGET bgfx::shaderc)
        message(FATAL_ERROR "bgfx::shaderc target not found. Cannot compile shaders.")
        return()
    endif()

    # 获取平台和profiles
    _bgfx_get_platform(PLATFORM)
    _bgfx_get_shader_profiles(PROFILES)

    # 设置BGFX shader include路径
    if(NOT DEFINED BGFX_SHADER_INCLUDE_PATH)
        if(DEFINED BGFX_DIR)
            set(BGFX_SHADER_INCLUDE_PATH "${BGFX_DIR}/src")
        else()
            # 尝试使用默认路径
            set(BGFX_DIR "${CMAKE_SOURCE_DIR}/thirdparty/bgfx.cmake/bgfx")
            set(BGFX_SHADER_INCLUDE_PATH "${BGFX_DIR}/src")
        endif()
    endif()
    
    if(NOT EXISTS "${BGFX_SHADER_INCLUDE_PATH}")
        message(FATAL_ERROR "BGFX shader include path does not exist: ${BGFX_SHADER_INCLUDE_PATH}. Please check BGFX_DIR is set correctly.")
        return()
    endif()

    set(ALL_OUTPUTS "")
    foreach(SHADER_FILE ${ARGS_SHADERS})
        get_filename_component(SHADER_FILE_BASENAME ${SHADER_FILE} NAME)
        get_filename_component(SHADER_FILE_NAME_WE ${SHADER_FILE} NAME_WE)
        get_filename_component(SHADER_FILE_ABSOLUTE ${SHADER_FILE} ABSOLUTE)

        # 构建输出目标和命令
        set(OUTPUTS "")
        set(COMMANDS "")
        set(MKDIR_COMMANDS "")
        foreach(PROFILE ${PROFILES})
            _bgfx_get_profile_path_ext(${PROFILE} PROFILE_PATH_EXT)
            _bgfx_get_profile_ext(${PROFILE} PROFILE_EXT)
            
            if(ARGS_AS_HEADERS)
                set(HEADER_PREFIX .h)
            endif()
            set(OUTPUT ${ARGS_OUTPUT_DIR}/${PROFILE_PATH_EXT}/${SHADER_FILE_BASENAME}.bin${HEADER_PREFIX})
            
            # 动态设置平台，SPIRV需要使用linux平台
            set(SHADER_PLATFORM ${PLATFORM})
            if(PROFILE STREQUAL "spirv")
                set(SHADER_PLATFORM "linux")
            endif()
            
            set(BIN2C_PART "")
            if(ARGS_AS_HEADERS)
                set(BIN2C_PART "--bin2c" "${SHADER_FILE_NAME_WE}_${PROFILE_EXT}")
            endif()
            
            # 构建include目录参数
            set(INCLUDE_FLAGS "")
            foreach(INCLUDE_DIR ${BGFX_SHADER_INCLUDE_PATH} ${ARGS_INCLUDE_DIRS})
                list(APPEND INCLUDE_FLAGS "-i" "${INCLUDE_DIR}")
            endforeach()
            
            list(APPEND OUTPUTS ${OUTPUT})
            list(APPEND ALL_OUTPUTS ${OUTPUT})
            list(
                APPEND
                MKDIR_COMMANDS
                COMMAND
                ${CMAKE_COMMAND}
                -E
                make_directory
                ${ARGS_OUTPUT_DIR}/${PROFILE_PATH_EXT}
            )
            list(APPEND COMMANDS 
                COMMAND $<TARGET_FILE:bgfx::shaderc>
                    --type ${ARGS_TYPE}
                    --platform ${SHADER_PLATFORM}
                    --profile ${PROFILE}
                    -f "${SHADER_FILE_ABSOLUTE}"
                    -o "${OUTPUT}"
                    ${INCLUDE_FLAGS}
                    --varyingdef "${ARGS_VARYING_DEF}"
                    -O "$<$<CONFIG:debug>:0>$<$<CONFIG:release>:3>$<$<CONFIG:relwithdebinfo>:3>$<$<CONFIG:minsizerel>:3>"
                    "$<$<BOOL:${ARGS_DEBUG_SHADERS}>:--debug>"
                    --Werror
                    ${BIN2C_PART}
            )
        endforeach()

        add_custom_command(
            OUTPUT ${OUTPUTS}
            ${MKDIR_COMMANDS} ${COMMANDS}
            MAIN_DEPENDENCY ${SHADER_FILE_ABSOLUTE}
            DEPENDS ${ARGS_VARYING_DEF}
            COMMENT "Compiling ${ARGS_TYPE} shader ${SHADER_FILE_NAME_WE}"
        )
    endforeach()

    if(DEFINED ARGS_OUT_FILES_VAR)
        set(${ARGS_OUT_FILES_VAR} ${ALL_OUTPUTS} PARENT_SCOPE)
    endif()
endfunction()

# 主函数：添加着色器目录
function(add_shaders_directory SHADERS_DIR TARGET_OUT_VAR)
    get_filename_component(SHADERS_DIR "${SHADERS_DIR}" ABSOLUTE)
    get_filename_component(NAMESPACE "${CMAKE_CURRENT_SOURCE_DIR}" NAME_WE)

    if (NOT EXISTS "${SHADERS_DIR}")
        message(NOTICE "Shaders directory does not exist")
        return()
    endif ()

    # 查找顶点着色器
    file(GLOB VERTEX_SHADER_FILES CONFIGURE_DEPENDS FOLLOW_SYMLINKS "${SHADERS_DIR}/*.sc")
    list(FILTER VERTEX_SHADER_FILES EXCLUDE REGEX "\.def\.sc$")
    list(FILTER VERTEX_SHADER_FILES INCLUDE REGEX "[\\\/]((vs_[^\\\/]*\.sc)|([^\\\/]*(\.vert)(\.sc)))$")

    # 查找片段着色器
    file(GLOB FRAGMENT_SHADER_FILES CONFIGURE_DEPENDS FOLLOW_SYMLINKS "${SHADERS_DIR}/*.sc")
    list(FILTER FRAGMENT_SHADER_FILES EXCLUDE REGEX "\.def\.sc$")
    list(FILTER FRAGMENT_SHADER_FILES INCLUDE REGEX "[\\\/]((fs_[^\\\/]*\.sc)|([^\\\/]*(\.frag)(\.sc)))$")

    if (NOT VERTEX_SHADER_FILES AND NOT FRAGMENT_SHADER_FILES)
        message(NOTICE "No shader files in directory")
        return()
    endif ()

    if (CMAKE_CROSSCOMPILING AND NOT ("${CMAKE_HOST_SYSTEM_NAME}" STREQUAL "${CMAKE_SYSTEM_NAME}"))
        message(STATUS "Not compiling shaders during cross-compilation")
        return()
    endif ()

    # 检查varying定义文件
    set(VARYING_DEF_LOCATION "${SHADERS_DIR}/varying.def.sc")
    if (NOT EXISTS "${VARYING_DEF_LOCATION}")
        message(WARNING "Varying def does not exist")
        return()
    endif ()

    # 设置输出目录
    set(SHADERS_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/include/generated/shaders/${NAMESPACE}")
    file(MAKE_DIRECTORY "${SHADERS_OUT_DIR}")

    # 编译顶点着色器
    bgfx_compile_shaders(
        TYPE vertex
        SHADERS ${VERTEX_SHADER_FILES}
        VARYING_DEF "${VARYING_DEF_LOCATION}"
        OUTPUT_DIR "${SHADERS_OUT_DIR}"
        OUT_FILES_VAR VERTEX_OUTPUT_FILES
        INCLUDE_DIRS "${SHADERS_DIR}"
        AS_HEADERS
    )

    # 编译片段着色器
    bgfx_compile_shaders(
        TYPE fragment
        SHADERS ${FRAGMENT_SHADER_FILES}
        VARYING_DEF "${VARYING_DEF_LOCATION}"
        OUTPUT_DIR "${SHADERS_OUT_DIR}"
        OUT_FILES_VAR FRAGMENT_OUTPUT_FILES
        INCLUDE_DIRS "${SHADERS_DIR}"
        AS_HEADERS
    )

    # 合并输出文件列表
    set(OUTPUT_FILES)
    list(APPEND OUTPUT_FILES ${VERTEX_OUTPUT_FILES})
    list(APPEND OUTPUT_FILES ${FRAGMENT_OUTPUT_FILES})

    list(LENGTH OUTPUT_FILES SHADER_COUNT)
    if (SHADER_COUNT EQUAL 0)
        return()
    endif ()

    # 生成包含所有着色器的头文件
    set(INCLUDE_ALL_HEADER "")
    string(APPEND INCLUDE_ALL_HEADER "// This file is generated by ShaderUtils.cmake\n")
    string(APPEND INCLUDE_ALL_HEADER "#include <stdint.h>\n")
    
    # 去重并获取正确的相对路径
    set(UNIQUE_INCLUDES "")
    foreach (OUTPUT_FILE IN LISTS OUTPUT_FILES)
        # 获取相对于 SHADERS_OUT_DIR 的路径
        file(RELATIVE_PATH REL_PATH "${SHADERS_OUT_DIR}" "${OUTPUT_FILE}")
        # 检查是否已经添加过这个include
        list(FIND UNIQUE_INCLUDES "${REL_PATH}" INDEX)
        if(INDEX EQUAL -1)
            list(APPEND UNIQUE_INCLUDES "${REL_PATH}")
            string(APPEND INCLUDE_ALL_HEADER "#include <generated/shaders/${NAMESPACE}/${REL_PATH}>\n")
        endif()
    endforeach ()
    file(WRITE "${SHADERS_OUT_DIR}/all.h" "${INCLUDE_ALL_HEADER}")
    list(APPEND OUTPUT_FILES "${SHADERS_OUT_DIR}/all.h")

    # 创建目标
    string(MD5 DIR_HASH "${SHADERS_DIR}")
    set(TARGET_NAME "Shaders_${DIR_HASH}")
    add_custom_target("${DIR_HASH}" ALL
        DEPENDS ${OUTPUT_FILES}
        SOURCES ${VARYING_DEF_LOCATION} ${FRAGMENT_SHADER_FILES} ${VERTEX_SHADER_FILES}
    )

    # 创建接口库
    add_library("${TARGET_NAME}" INTERFACE)
    add_dependencies("${TARGET_NAME}" bgfx::shaderc "${DIR_HASH}")
    target_include_directories("${TARGET_NAME}" INTERFACE "${CMAKE_CURRENT_BINARY_DIR}/include")

    # 返回目标名称
    set("${TARGET_OUT_VAR}" "${TARGET_NAME}" PARENT_SCOPE)
endfunction()
