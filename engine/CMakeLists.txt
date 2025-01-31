# 设置引擎库的版本
set(TINA_ENGINE_VERSION ${PROJECT_VERSION})

# 添加 cmake 模块路径
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/modules")

# 包含资源复制模块和EASTL设置
include(CopyResources)
include(SetupEASTL)

# 设置EASTL
setup_eastl()

# 添加第三方库
add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/bgfx.cmake bgfx_build)

# 首先添加Core模块，因为其他模块可能依赖它
add_subdirectory(core)    # 基础功能模块

# 创建引擎总库（作为接口库）
add_library(TinaEngine INTERFACE)
add_library(Tina::Engine ALIAS TinaEngine)

# 链接Core模块（这是必需的基础模块）
target_link_libraries(TinaEngine
    INTERFACE
        Tina::Core    # 核心模块
)

# 可选模块：只有在它们存在时才添加
#if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/network/CMakeLists.txt")
#    add_subdirectory(network) # 网络功能模块
#    target_link_libraries(TinaEngine INTERFACE Tina::Network)
#endif()
#
#if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/utils/CMakeLists.txt")
#    add_subdirectory(utils)   # 工具功能模块
#    target_link_libraries(TinaEngine INTERFACE Tina::Utils)
#endif()

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
install(TARGETS TinaEngine TinaCore
    EXPORT TinaTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include
)

# 安装头文件 - 包含所有常见的头文件类型
install(DIRECTORY core/include/
    DESTINATION include
    FILES_MATCHING 
        PATTERN "*.h"      # C头文件
        PATTERN "*.hpp"    # C++头文件
        PATTERN "*.hxx"    # C++头文件（替代扩展名）
        PATTERN "*.inl"    # 内联实现文件
        PATTERN "*.tpp"    # 模板实现文件
        PATTERN "*.inc"    # 包含文件
    PATTERN "private" EXCLUDE  # 排除private目录
    PATTERN "internal" EXCLUDE # 排除internal目录
)

install(EXPORT TinaTargets
    FILE TinaTargets.cmake
    NAMESPACE Tina::
    DESTINATION lib/cmake/Tina
)
