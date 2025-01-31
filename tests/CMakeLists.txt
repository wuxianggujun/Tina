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