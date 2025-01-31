#include <gtest/gtest.h>
#include "tina/core/Core.hpp"
#include <EASTL/vector.h>
#include <EASTL/string.h>

#include "tina/core/Engine.hpp"

class EngineTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 在每个测试之前运行
    }

    void TearDown() override {
        // 在每个测试之后运行
    }

    Tina::Core::Engine engine;
};

// 测试引擎初始化
TEST_F(EngineTest, Initialization) {
    EXPECT_TRUE(engine.initialize()) << "Engine initialization should succeed";
}

// 测试引擎版本
TEST_F(EngineTest, Version) {
    const char* version = engine.getVersion();
    EXPECT_NE(version, nullptr) << "Version should not be null";
    EXPECT_STRNE(version, "") << "Version should not be empty";
}

// 测试完整的生命周期
TEST_F(EngineTest, Lifecycle) {
    EXPECT_TRUE(engine.initialize()) << "Engine initialization should succeed";
    engine.shutdown();
    // 如果shutdown没有崩溃，我们认为测试通过
    SUCCEED() << "Engine shutdown completed successfully";
}

// EASTL需要实现这个分配器回调
void* operator new[](size_t size, const char* pName, int flags, unsigned debugFlags, const char* file, int line)
{
    return new uint8_t[size];
}

void* operator new[](size_t size, size_t alignment, size_t alignmentOffset, const char* pName, int flags, unsigned debugFlags, const char* file, int line)
{
    return new uint8_t[size];
}

TEST(EASTLTest, VectorTest) {
    eastl::vector<int> vec;
    vec.push_back(1);
    vec.push_back(2);
    vec.push_back(3);

    EXPECT_EQ(vec.size(), 3);
    EXPECT_EQ(vec[0], 1);
    EXPECT_EQ(vec[1], 2);
    EXPECT_EQ(vec[2], 3);
}

TEST(EASTLTest, StringTest) {
    eastl::string str = "Hello EASTL";
    EXPECT_EQ(str.length(), 11);
    EXPECT_TRUE(str == "Hello EASTL");
}

int main(int argc, char **argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
} 