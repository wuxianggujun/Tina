# 设置测试目录
# 添加本地的Google Test
add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/googletest googletest_build)

# 启用测试
enable_testing()

# 添加测试可执行文件
add_executable(CoreTest
    core/CoreTest.cpp
)

# 链接依赖库
target_link_libraries(CoreTest
    PRIVATE
        TinaEngine
        GTest::gtest
        GTest::gtest_main
)

# 设置包含目录
target_include_directories(CoreTest
    PRIVATE
        ${CMAKE_SOURCE_DIR}/engine/core/include
)

# 添加到CTest
include(GoogleTest)
gtest_discover_tests(CoreTest)

# 添加String测试可执行文件
add_executable(LoggerTest
    log/LoggerTest.cpp
)

# 链接必要的库
target_link_libraries(LoggerTest
    PRIVATE
        Tina::Core
        GTest::gtest
        GTest::gtest_main
)

# 添加包含目录
target_include_directories(LoggerTest
    PRIVATE
        ${CMAKE_SOURCE_DIR}/engine/core/include
)

# 添加测试
gtest_discover_tests(LoggerTest)