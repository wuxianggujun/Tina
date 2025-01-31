# 函数：复制资源到目标目录
# 参数：
#   TARGET_NAME - 目标名称（用于添加依赖）
#   SOURCE_DIR - 源资源目录
#   DESTINATION_DIR - 目标目录
#   PATTERNS - 要复制的文件模式（可选，默认复制所有文件）
function(copy_resources)
    # 解析参数
    set(options "")
    set(oneValueArgs TARGET_NAME SOURCE_DIR DESTINATION_DIR)
    set(multiValueArgs PATTERNS)
    cmake_parse_arguments(COPY "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    # 验证必需参数
    if(NOT COPY_TARGET_NAME)
        message(FATAL_ERROR "copy_resources: TARGET_NAME is required")
    endif()
    if(NOT COPY_SOURCE_DIR)
        message(FATAL_ERROR "copy_resources: SOURCE_DIR is required")
    endif()
    if(NOT COPY_DESTINATION_DIR)
        message(FATAL_ERROR "copy_resources: DESTINATION_DIR is required")
    endif()

    # 如果没有指定模式，默认复制所有文件
    if(NOT COPY_PATTERNS)
        set(COPY_PATTERNS "*")
    endif()

    # 确保目标目录存在
    file(MAKE_DIRECTORY ${COPY_DESTINATION_DIR})

    # 为每个模式创建文件列表
    set(ALL_FILES "")
    foreach(PATTERN ${COPY_PATTERNS})
        file(GLOB_RECURSE FILES "${COPY_SOURCE_DIR}/${PATTERN}")
        list(APPEND ALL_FILES ${FILES})
    endforeach()

    # 创建自定义命令来复制文件
    foreach(FILE ${ALL_FILES})
        # 计算相对路径
        file(RELATIVE_PATH REL_PATH "${COPY_SOURCE_DIR}" "${FILE}")
        set(DEST_FILE "${COPY_DESTINATION_DIR}/${REL_PATH}")

        # 获取目标文件的目录
        get_filename_component(DEST_DIR "${DEST_FILE}" DIRECTORY)
        
        # 添加复制命令
        add_custom_command(
            TARGET ${COPY_TARGET_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${DEST_DIR}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FILE}" "${DEST_FILE}"
            DEPENDS "${FILE}"
            COMMENT "Copying resource ${REL_PATH}"
        )
    endforeach()

    # 添加一个信息性消息
    message(STATUS "Resource copy configured for target ${COPY_TARGET_NAME}")
    message(STATUS "  From: ${COPY_SOURCE_DIR}")
    message(STATUS "  To: ${COPY_DESTINATION_DIR}")
endfunction() 