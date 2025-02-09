# 设置引擎库的版本
set(TINA_ENGINE_VERSION ${PROJECT_VERSION})

# 添加 cmake 模块路径
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/modules")

# 包含资源复制模块和EASTL设置
include(CopyResources)
include(ThirdParty)
include(Modules)

# 添加第三方库
# 添加第三方库（带配置选项）
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

# 创建引擎总库（作为接口库）
add_library(TinaEngine INTERFACE)
add_library(Tina::Engine ALIAS TinaEngine)

# 链接Core模块（这是必需的基础模块）
tina_add_module(NAME core REQUIRED)
#tina_add_module(NAME network)
#tina_add_module(NAME utils)

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
