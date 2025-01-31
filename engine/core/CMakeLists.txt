# 收集源文件
file(GLOB_RECURSE CORE_SOURCES
        "src/*.cpp"
        "src/*.c"
)

file(GLOB_RECURSE CORE_HEADERS
        "include/*.hpp"
        "include/*.h"
)

# 创建核心库
add_library(TinaCore ${TINA_LIB_TYPE}
        ${CORE_SOURCES}
        ${CORE_HEADERS}
)
add_library(Tina::Core ALIAS TinaCore)

# 设置包含目录
target_include_directories(TinaCore
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# 链接依赖库
target_link_libraries(TinaCore
        PUBLIC
        bgfx
        bx
        bimg
        EASTL
)

# 设置编译选项
target_compile_features(TinaCore PUBLIC cxx_std_17)

# 添加版本定义和导出宏
target_compile_definitions(TinaCore
        PRIVATE
        TINA_VERSION="${PROJECT_VERSION}"
        TINA_CORE_EXPORTS
        PUBLIC
        $<$<BOOL:${BUILD_SHARED_LIBS}>:TINA_SHARED_LIB>
)

# 设置库属性
set_target_properties(TinaCore PROPERTIES
        VERSION "${TINA_ENGINE_VERSION}"
        SOVERSION "1"
        EXPORT_NAME "Core"
        OUTPUT_NAME "tina-core"
)
# # 安装配置, 只需要安装库文件和头文件即可，导出已经在 engine/CMakeLists.txt 中处理
# install(TARGETS TinaCore
#         LIBRARY DESTINATION lib
#         ARCHIVE DESTINATION lib
#         RUNTIME DESTINATION bin
#         INCLUDES DESTINATION include
# )

# # 安装头文件
# install(DIRECTORY include/
#         DESTINATION include
#         FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
# )
