# 设置测试目录
# 添加本地的Google Test
add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/googletest googletest_build)

# 启用测试
enable_testing()

# 添加测试可执行文件
add_executable(core_tests
    core/CoreTest.cpp
)

# 链接Google Test和被测试的库
target_link_libraries(core_tests
    PRIVATE
        gtest
        gtest_main
        TinaEngine
)

# 设置包含目录
target_include_directories(core_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/engine/core/include
)

# 添加到CTest
include(GoogleTest)
gtest_discover_tests(core_tests) 