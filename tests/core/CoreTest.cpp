#include <gtest/gtest.h>
#include "tina/core/Core.hpp"

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

int main(int argc, char **argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
} 