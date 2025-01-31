# 设置测试目录
# 添加本地的Google Test
add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/googletest googletest_build)

# 添加本地的Google Benchmark
add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/benchmark benchmark_build)

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
add_executable(StringTest
    container/StringTest.cpp
)

# 链接必要的库
target_link_libraries(StringTest
    PRIVATE
        Tina::Core
        GTest::gtest
        GTest::gtest_main
        benchmark
        benchmark_main
)

# 添加包含目录
target_include_directories(StringTest
    PRIVATE
        ${CMAKE_SOURCE_DIR}/engine
)

# 添加测试
add_test(NAME StringTest COMMAND StringTest) 