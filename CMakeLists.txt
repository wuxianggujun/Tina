cmake_minimum_required(VERSION 3.15)

# 项目名称和版本
project(Tina 
    VERSION 1.0.0
    DESCRIPTION "Tina Engine - A Modular Engine"
    LANGUAGES C CXX
)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 设置MSVC的UTF-8编码支持
if(MSVC)
    add_compile_options(/utf-8)
    # 为GTest设置正确的运行时库
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
endif()

# 设置输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# 选项配置
option(TINA_BUILD_SHARED_LIBS "Build Tina as shared libraries" OFF)
option(TINA_BUILD_EXAMPLES "Build example applications" ON)
option(TINA_BUILD_TESTS "Build tests" ON)

# 设置库类型（静态或动态）
if(TINA_BUILD_SHARED_LIBS)
    set(BUILD_SHARED_LIBS ON)
    set(TINA_LIB_TYPE SHARED)
else()
    set(BUILD_SHARED_LIBS OFF)
    set(TINA_LIB_TYPE STATIC)
endif()

# 添加子目录
add_subdirectory(engine)

# 构建示例（可选）
if(TINA_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# 构建测试（可选）
if(TINA_BUILD_TESTS)
    add_subdirectory(tests)
endif()

# 导出配置
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_BINARY_DIR}/TinaConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

# 安装配置
install(EXPORT TinaTargets
    FILE TinaTargets.cmake
    NAMESPACE Tina::
    DESTINATION lib/cmake/Tina
)

# 创建配置文件
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/TinaConfig.cmake.in
    ${CMAKE_BINARY_DIR}/TinaConfig.cmake
    @ONLY
)
