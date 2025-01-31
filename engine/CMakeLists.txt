# 设置引擎库的版本
set(TINA_ENGINE_VERSION ${PROJECT_VERSION})

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

# 安装配置
install(TARGETS TinaEngine
    EXPORT TinaTargets
    INCLUDES DESTINATION include
)
