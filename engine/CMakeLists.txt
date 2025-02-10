# 设置引擎库的版本
set(TINA_ENGINE_VERSION ${PROJECT_VERSION})

# 添加 cmake 模块路径
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/modules")

# 包含资源复制模块和其他设置
include(CopyResources)
include(ThirdParty)
include(Modules)
include(CompileShaders)

# 添加第三方库
tina_add_third_party(NAME bgfx
        PATH ${CMAKE_SOURCE_DIR}/third_party/bgfx.cmake
        OPTIONS
        BGFX_BUILD_TOOLS=ON
        BGFX_BUILD_EXAMPLES=OFF)

# 设置 BGFX 根目录 - 这对着色器编译很重要
set(BGFX_DIR "${CMAKE_SOURCE_DIR}/third_party/bgfx.cmake/bgfx")
message(STATUS "BGFX_DIR: ${BGFX_DIR}")

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

# 创建引擎库（改为INTERFACE库，因为它只是一个聚合库）
add_library(TinaEngine INTERFACE)
add_library(Tina::Engine ALIAS TinaEngine)

# 链接Core模块（这是必需的基础模块）
tina_add_module(NAME core REQUIRED)

# 设置引擎库的包含目录和链接依赖
target_include_directories(TinaEngine
        INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(TinaEngine
        INTERFACE
        Tina::Core
        bgfx
        bx
        bimg
        fmt::fmt
        glfw
        EnTT::EnTT
        glm
)

## 复制资源文件
#add_custom_target(copy_resources ALL
#    COMMAND ${CMAKE_COMMAND} -E copy_directory
#    "${CMAKE_CURRENT_SOURCE_DIR}/resources"
#    "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/resources"
#    COMMENT "Copying resources..."
#)
#
#add_dependencies(TinaEngine copy_resources)


