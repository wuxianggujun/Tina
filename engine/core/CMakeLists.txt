# 收集源文件
file(GLOB_RECURSE CORE_SOURCE_FILES
        "src/*.cpp"
        "src/*.c"
)

file(GLOB_RECURSE CORE_HEADER_FILES
        "include/*.hpp"
        "include/*.h"
)

# 创建核心库
add_library(TinaCore ${CORE_SOURCE_FILES} ${CORE_HEADER_FILES})
add_library(Tina::Core ALIAS TinaCore)

# 设置包含目录
target_include_directories(TinaCore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# 链接依赖库
target_link_libraries(TinaCore
    PUBLIC
        bx
        bgfx
        bimg
        fmt::fmt
        glfw
        EnTT::EnTT
        glm
)

# 设置编译选项
target_compile_definitions(TinaCore
    PRIVATE
        TINA_CORE_EXPORTS
        TINA_VERSION="${PROJECT_VERSION}"
)

# 启用 C++17
target_compile_features(TinaCore PUBLIC cxx_std_17)
