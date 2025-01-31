# 设置引擎库的版本
set(TINA_ENGINE_VERSION ${PROJECT_VERSION})

# 添加 cmake 模块路径
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/modules")

# 包含资源复制模块
include(CopyResources)

# 添加 bgfx
add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/bgfx.cmake bgfx_build)

# 添加子模块
add_subdirectory(core)
#add_subdirectory(network)
#add_subdirectory(utils)

# 创建引擎总库（作为接口库）
add_library(TinaEngine INTERFACE)
add_library(Tina::Engine ALIAS TinaEngine)

# 链接所有子模块
target_link_libraries(TinaEngine
    INTERFACE
        Tina::Core
#        Tina::Network
#        Tina::Utils
)

# 设置引擎库的属性
set_target_properties(TinaEngine 
    PROPERTIES
        EXPORT_NAME "Engine"
)

# 复制资源文件
copy_resources(
    TARGET_NAME TinaEngine
    SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/resources"
    DESTINATION_DIR "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/resources"
    PATTERNS
        "shaders/*"
        "textures/*"
        "icons/*"
        "fonts/*"
)

# 安装配置
install(TARGETS TinaEngine
    EXPORT TinaTargets
    INCLUDES DESTINATION include
)

# 安装资源文件
install(DIRECTORY resources/
    DESTINATION share/tina/resources
    FILES_MATCHING
        PATTERN "shaders/*"
        PATTERN "textures/*"
        PATTERN "icons/*"
        PATTERN "fonts/*"
)
