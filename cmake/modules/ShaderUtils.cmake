cmake_minimum_required(VERSION 3.20)

# 获取平台相关的着色器配置
function(_get_shader_profiles PLATFORM_OUT PROFILES_OUT SHADER_MODELS_OUT)
    if(WIN32 OR MINGW OR MSYS OR CYGWIN)
        set(PLATFORM "windows")
        set(PROFILES "dx11;dx9;spirv;essl;glsl")
        set(SHADER_MODELS "s_5_0;s_4_0;spirv;300_es;120")
    elseif(APPLE)
        set(PLATFORM "osx")
        set(PROFILES "metal;spirv;glsl")
        set(SHADER_MODELS "metal;spirv;120")
    elseif(UNIX AND NOT APPLE)
        set(PLATFORM "linux")
        set(PROFILES "glsl;spirv;essl")
        set(SHADER_MODELS "120;spirv;300_es")
    else()
        message(FATAL_ERROR "Unsupported platform for shader compilation")
    endif()
    
    set(${PLATFORM_OUT} ${PLATFORM} PARENT_SCOPE)
    set(${PROFILES_OUT} ${PROFILES} PARENT_SCOPE)
    set(${SHADER_MODELS_OUT} ${SHADER_MODELS} PARENT_SCOPE)
endfunction()

# 编译着色器目录
function(add_shader_compile_dir SHADER_DIR)
    cmake_parse_arguments(ARGS "GENERATE_HEADERS" "" "" ${ARGN})
    
    if(NOT EXISTS "${SHADER_DIR}")
        message(FATAL_ERROR "Shader directory ${SHADER_DIR} does not exist")
        return()
    endif()

    # 检查shaderc是否可用
    if(NOT TARGET bgfx::shaderc)
        message(FATAL_ERROR "bgfx::shaderc target not found. Cannot compile shaders.")
        return()
    endif()

    # 设置输出目录
    set(SHADER_OUTPUT_DIR "${CMAKE_BINARY_DIR}/resources/shaders")
    file(MAKE_DIRECTORY "${SHADER_OUTPUT_DIR}")
    message(STATUS "Shader output directory: ${SHADER_OUTPUT_DIR}")

    # 设置 bgfx 相关路径
    set(BGFX_SHADER_INCLUDE_PATH "${BGFX_DIR}/src")
    if(NOT EXISTS "${BGFX_SHADER_INCLUDE_PATH}")
        message(FATAL_ERROR "BGFX shader include path does not exist: ${BGFX_SHADER_INCLUDE_PATH}")
        return()
    endif()

    # 获取平台相关配置
    _get_shader_profiles(PLATFORM PROFILES SHADER_MODELS)
    message(STATUS "Compiling shaders for platform: ${PLATFORM}")
    message(STATUS "Using shader profiles: ${PROFILES}")
    message(STATUS "Using shader models: ${SHADER_MODELS}")

    set(ALL_SHADER_OUTPUTS "")
    set(ALL_SHADER_SOURCES "")

    # 获取着色器名称列表（移除GENERATE_HEADERS参数）
    set(SHADER_NAMES ${ARGS_UNPARSED_ARGUMENTS})

    # 处理每个着色器
    foreach(SHADER_NAME ${SHADER_NAMES})
        set(VERTEX_SHADER "${SHADER_DIR}/vs_${SHADER_NAME}.sc")
        set(FRAGMENT_SHADER "${SHADER_DIR}/fs_${SHADER_NAME}.sc")
        
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
        list(LENGTH PROFILES PROFILE_COUNT)
        math(EXPR LAST_INDEX "${PROFILE_COUNT} - 1")
        
        foreach(INDEX RANGE ${LAST_INDEX})
            list(GET PROFILES ${INDEX} PROFILE)
            list(GET SHADER_MODELS ${INDEX} SHADER_MODEL)
            
            set(PROFILE_OUTPUT_DIR "${SHADER_OUTPUT_DIR}/${PROFILE}")
            file(MAKE_DIRECTORY "${PROFILE_OUTPUT_DIR}")

            # 顶点着色器
            set(VERTEX_OUTPUT "${PROFILE_OUTPUT_DIR}/${SHADER_NAME}.vs.bin")
            set(VERTEX_HEADER "${PROFILE_OUTPUT_DIR}/${SHADER_NAME}.vs.hpp")

            # 基本的编译命令（始终生成二进制文件）
            add_custom_command(
                OUTPUT ${VERTEX_OUTPUT}
                COMMAND bgfx::shaderc
                    --type vertex
                    --platform ${PLATFORM}
                    --profile ${SHADER_MODEL}
                    -f "${VERTEX_SHADER}"
                    -o "${VERTEX_OUTPUT}"
                    -i "${BGFX_SHADER_INCLUDE_PATH}"
                    -i "${SHADER_DIR}"
                    --varyingdef "${VARYING_DEF_FILE}"
                DEPENDS "${VERTEX_SHADER}" "${VARYING_DEF_FILE}"
                COMMENT "Compiling vertex shader ${SHADER_NAME} for ${PROFILE}"
                VERBATIM
            )

            list(APPEND ALL_SHADER_OUTPUTS "${VERTEX_OUTPUT}")

            # 如果需要生成头文件
            if(ARGS_GENERATE_HEADERS)
                add_custom_command(
                    OUTPUT ${VERTEX_HEADER}
                    COMMAND bgfx::shaderc
                        --type vertex
                        --platform ${PLATFORM}
                        --profile ${SHADER_MODEL}
                        -f "${VERTEX_SHADER}"
                        -o "${VERTEX_HEADER}"
                        -i "${BGFX_SHADER_INCLUDE_PATH}"
                        -i "${SHADER_DIR}"
                        --varyingdef "${VARYING_DEF_FILE}"
                        --bin2c
                    DEPENDS "${VERTEX_SHADER}" "${VARYING_DEF_FILE}"
                    COMMENT "Generating vertex shader header ${SHADER_NAME} for ${PROFILE}"
                    VERBATIM
                )
                list(APPEND ALL_SHADER_OUTPUTS "${VERTEX_HEADER}")
            endif()

            # 片段着色器
            set(FRAGMENT_OUTPUT "${PROFILE_OUTPUT_DIR}/${SHADER_NAME}.fs.bin")
            set(FRAGMENT_HEADER "${PROFILE_OUTPUT_DIR}/${SHADER_NAME}.fs.hpp")

            # 基本的编译命令（始终生成二进制文件）
            add_custom_command(
                OUTPUT ${FRAGMENT_OUTPUT}
                COMMAND bgfx::shaderc
                    --type fragment
                    --platform ${PLATFORM}
                    --profile ${SHADER_MODEL}
                    -f "${FRAGMENT_SHADER}"
                    -o "${FRAGMENT_OUTPUT}"
                    -i "${BGFX_SHADER_INCLUDE_PATH}"
                    -i "${SHADER_DIR}"
                    --varyingdef "${VARYING_DEF_FILE}"
                DEPENDS "${FRAGMENT_SHADER}" "${VARYING_DEF_FILE}"
                COMMENT "Compiling fragment shader ${SHADER_NAME} for ${PROFILE}"
                VERBATIM
            )

            list(APPEND ALL_SHADER_OUTPUTS "${FRAGMENT_OUTPUT}")

            # 如果需要生成头文件
            if(ARGS_GENERATE_HEADERS)
                add_custom_command(
                    OUTPUT ${FRAGMENT_HEADER}
                    COMMAND bgfx::shaderc
                        --type fragment
                        --platform ${PLATFORM}
                        --profile ${SHADER_MODEL}
                        -f "${FRAGMENT_SHADER}"
                        -o "${FRAGMENT_HEADER}"
                        -i "${BGFX_SHADER_INCLUDE_PATH}"
                        -i "${SHADER_DIR}"
                        --varyingdef "${VARYING_DEF_FILE}"
                        --bin2c
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

        message(STATUS "Created shader target with outputs:")
        foreach(OUTPUT ${ALL_SHADER_OUTPUTS})
            message(STATUS "  ${OUTPUT}")
        endforeach()
    else()
        message(FATAL_ERROR "No shader outputs were configured")
    endif()
endfunction()

# 编译着色器到头文件的函数
function(bgfx_compile_shader_to_header)
    cmake_parse_arguments(ARGS "" "TYPE;VARYING_DEF;OUTPUT_DIR;OUT_FILES_VAR" "SHADERS;INCLUDE_DIRS" ${ARGN})
    
    if(NOT ARGS_TYPE OR NOT ARGS_VARYING_DEF OR NOT ARGS_OUTPUT_DIR OR NOT ARGS_OUT_FILES_VAR OR NOT ARGS_SHADERS)
        message(FATAL_ERROR "Missing required arguments")
        return()
    endif()
    
    # 确保输出目录存在
    file(MAKE_DIRECTORY "${ARGS_OUTPUT_DIR}")
    message(STATUS "Shader output directory: ${ARGS_OUTPUT_DIR}")
    
    set(OUTPUT_FILES)
    
    foreach(SHADER ${ARGS_SHADERS})
        get_filename_component(SHADER_NAME "${SHADER}" NAME_WE)
        set(OUTPUT_FILE "${ARGS_OUTPUT_DIR}/${SHADER_NAME}.bin.h")
        message(STATUS "Processing shader: ${SHADER}")
        message(STATUS "Output file will be: ${OUTPUT_FILE}")
        
        set(INCLUDE_FLAGS "")
        foreach(INCLUDE_DIR ${ARGS_INCLUDE_DIRS})
            list(APPEND INCLUDE_FLAGS "-i" "${INCLUDE_DIR}")
        endforeach()
        
        if(WIN32)
            set(PLATFORM "windows")
            if(ARGS_TYPE STREQUAL "VERTEX")
                set(PROFILE "vs_5_0")
            else()
                set(PROFILE "ps_5_0")
            endif()
        elseif(APPLE)
            set(PLATFORM "osx")
            set(PROFILE "metal")
        else()
            set(PLATFORM "linux")
            set(PROFILE "120")
        endif()
        
        # 创建临时二进制文件
        set(TEMP_BIN "${CMAKE_CURRENT_BINARY_DIR}/temp/${SHADER_NAME}.bin")
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/temp")
        
        add_custom_command(
            OUTPUT "${OUTPUT_FILE}"
            COMMAND bgfx::shaderc
                -f "${SHADER}"
                -o "${TEMP_BIN}"
                ${INCLUDE_FLAGS}
                --platform ${PLATFORM}
                --type ${ARGS_TYPE}
                --profile ${PROFILE}
                -v "${ARGS_VARYING_DEF}"
            COMMAND bgfx::shaderc
                -f "${SHADER}"
                -o "${OUTPUT_FILE}"
                ${INCLUDE_FLAGS}
                --platform ${PLATFORM}
                --type ${ARGS_TYPE}
                --profile ${PROFILE}
                -v "${ARGS_VARYING_DEF}"
                --bin2c
            DEPENDS "${SHADER}" "${ARGS_VARYING_DEF}"
            COMMENT "Compiling ${ARGS_TYPE} shader ${SHADER_NAME} to header"
            VERBATIM
        )
        
        list(APPEND OUTPUT_FILES "${OUTPUT_FILE}")
    endforeach()
    
    set(${ARGS_OUT_FILES_VAR} "${OUTPUT_FILES}" PARENT_SCOPE)
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
    bgfx_compile_shader_to_header(
        TYPE VERTEX
        SHADERS ${VERTEX_SHADER_FILES}
        VARYING_DEF "${VARYING_DEF_LOCATION}"
        OUTPUT_DIR "${SHADERS_OUT_DIR}"
        OUT_FILES_VAR VERTEX_OUTPUT_FILES
        INCLUDE_DIRS "${SHADERS_DIR}" "${BGFX_DIR}/src"
    )

    # 编译片段着色器
    bgfx_compile_shader_to_header(
        TYPE FRAGMENT
        SHADERS ${FRAGMENT_SHADER_FILES}
        VARYING_DEF "${VARYING_DEF_LOCATION}"
        OUTPUT_DIR "${SHADERS_OUT_DIR}"
        OUT_FILES_VAR FRAGMENT_OUTPUT_FILES
        INCLUDE_DIRS "${SHADERS_DIR}" "${BGFX_DIR}/src"
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
    foreach (OUTPUT_FILE IN LISTS OUTPUT_FILES)
        get_filename_component(OUTPUT_FILENAME "${OUTPUT_FILE}" NAME)
        string(APPEND INCLUDE_ALL_HEADER "#include <generated/shaders/${NAMESPACE}/${OUTPUT_FILENAME}>\n")
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
