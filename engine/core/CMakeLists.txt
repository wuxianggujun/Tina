# 收集源文件
file(GLOB_RECURSE CORE_SOURCE_FILES
    "src/*.cpp"
    "src/*.c"
)

file(GLOB_RECURSE CORE_HEADER_FILES
    "include/*.hpp"
    "include/*.h"
)

# 编译着色器
add_shader_compile_dir(${CMAKE_SOURCE_DIR}/engine/resources/shaders "2d")

# 创建核心库
add_library(TinaCore ${CORE_SOURCE_FILES} ${CORE_HEADER_FILES})
add_library(Tina::Core ALIAS TinaCore)

# 确保着色器在核心库之前编译
add_dependencies(TinaCore shaders)

# 设置包含目录
target_include_directories(TinaCore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# 链接依赖库
target_link_libraries(TinaCore
    PUBLIC
        bx
        bgfx
        bimg
        bimg_decode
        fmt::fmt
        glfw
        EnTT::EnTT
        glm
        Tracy::TracyClient
)

# 设置编译选项
target_compile_definitions(TinaCore
    PRIVATE
        TINA_CORE_EXPORTS
        TINA_VERSION="${PROJECT_VERSION}"
        SHADER_OUTPUT_DIR="${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/resources/shaders"
)

# 启用 C++17
target_compile_features(TinaCore PUBLIC cxx_std_17)

# 导出符号
if(MSVC)
    target_compile_options(TinaCore PRIVATE /wd4251)
endif()

# 复制资源文件（排除shaders目录）
copy_resources(
    TARGET_NAME TinaCore
    SOURCE_DIR "${CMAKE_SOURCE_DIR}/engine/resources"
    DESTINATION_DIR "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/resources"
    PATTERNS 
        "textures/*"
        "fonts/*"
        "models/*"
        "audio/*"
)
