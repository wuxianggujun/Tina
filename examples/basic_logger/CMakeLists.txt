# 创建基础示例可执行文件
add_executable(basic_logger
    main.cpp
)

# 链接 Tina 引擎
target_link_libraries(basic_logger
    PRIVATE
        TinaEngine
)

# 设置包含目录
target_include_directories(basic_logger
    PRIVATE
        ${CMAKE_SOURCE_DIR}/engine/core/include
        ${CMAKE_SOURCE_DIR}/engine/network/include
        ${CMAKE_SOURCE_DIR}/engine/utils/include
) 