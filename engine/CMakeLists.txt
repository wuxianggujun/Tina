# 设置引擎库的版本
set(TINA_ENGINE_VERSION ${PROJECT_VERSION})

# 添加 cmake 模块路径
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/modules")

# 包含资源复制模块和其他设置
include(CopyResources)
include(ThirdParty)
include(Modules)
include(ShaderUtils)

# 添加第三方库
tina_add_third_party(NAME bgfx
        PATH ${CMAKE_SOURCE_DIR}/third_party/bgfx.cmake)

tina_add_third_party(NAME fmt
        PATH ${CMAKE_SOURCE_DIR}/third_party/fmt)

tina_add_third_party(NAME glfw
        PATH ${CMAKE_SOURCE_DIR}/third_party/glfw
        OPTIONS
        GLFW_BUILD_TESTS=OFF
        GLFW_BUILD_DOCS=OFF
        GLFW_INSTALL=OFF)

# GLM 配置
add_library(glm INTERFACE)
target_include_directories(glm INTERFACE ${CMAKE_SOURCE_DIR}/third_party/glm)

tina_add_third_party(NAME entt
        PATH ${CMAKE_SOURCE_DIR}/third_party/entt
        OPTIONS
        ENTT_BUILD_TESTING=OFF
        ENTT_BUILD_DOCS=OFF)

# 设置 BGFX 根目录
set(BGFX_ROOT_DIR "${CMAKE_SOURCE_DIR}/third_party/bgfx.cmake/bgfx")

# 创建引擎库（改为静态库而不是接口库）
add_library(TinaEngine STATIC dummy.cpp)
add_library(Tina::Engine ALIAS TinaEngine)

# 编译着色器
add_shader_compile_dir(${CMAKE_CURRENT_SOURCE_DIR}/resources/shaders
        2d  # This will compile vs_2d.sc and fs_2d.sc
        # Add more shader names here if needed
)
# 链接Core模块（这是必需的基础模块）
tina_add_module(NAME core REQUIRED)
#tina_add_module(NAME network)
#tina_add_module(NAME utils)

# 复制资源文件
add_custom_target(copy_resources ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/resources"
        "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/resources"
    COMMENT "Copying resources..."
)

# 确保 Core 模块可以找到生成的着色器头文件
target_include_directories(TinaCore 
    PRIVATE 
        "${CMAKE_BINARY_DIR}/generated"
)

# 添加依赖关系
add_dependencies(TinaCore shaders copy_resources)
