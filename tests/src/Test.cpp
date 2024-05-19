// main_test.cpp
#include <gtest/gtest.h>

// 测试套件示例
class BasicTests : public ::testing::Test {
protected:
    void SetUp() override {
        // 在每个测试用例运行之前设置
    }

    void TearDown() override {
        // 在每个测试用例运行之后清理
    }
};

int gint = 1;

int globalFunc(int i){
    return i+gint;
}

// 测试用例示例
TEST_F(BasicTests, Test1) {
    globalFunc(100);
}

TEST(BasicTests2, Test2) {
    EXPECT_TRUE(true); // 测试真值
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}