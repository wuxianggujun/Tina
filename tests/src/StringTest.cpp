//
// Created by wuxianggujun on 2025/1/26.
//
#include <gtest/gtest.h>
#include "base/String.hpp"

using namespace Tina;

// 在 SetUp 和 TearDown 中添加统计信息重置和打印
class StringTest : public testing::Test {
protected:
    void SetUp() override {
        StringMemoryPool::getInstance().resetStats();
        fmt::print("\n=== Test Start ===\n");
    }

    void TearDown() override {
        fmt::print("\n=== Test End ===\n");
        StringMemoryPool::getInstance().printStats();
    }
};

TEST_F(StringTest,ConstructorTest)
{
    String s1;
    EXPECT_TRUE(s1.empty());
    EXPECT_EQ(s1.size(),0);
    EXPECT_STREQ(s1.c_str(),"");


    String s2("Hello");
    EXPECT_FALSE(s2.empty());
    EXPECT_EQ(s2.size(),5);
    EXPECT_STREQ(s2.c_str(),"Hello");

    std::string stdStr = "World";
    String s3(stdStr);
    EXPECT_EQ(s3.size(),5);
    EXPECT_STREQ(s3.c_str(),"World");

    std::string_view sv = "Test";
    String s4(sv);
    EXPECT_EQ(s4.size(),4);
    EXPECT_STREQ(s4.c_str(),"Test");
}

// 测试移动构造和赋值
TEST_F(StringTest, MoveTest) {
    String original("Original");
    String moved(std::move(original));

    EXPECT_TRUE(original.empty());  // 原字符串应该为空
    EXPECT_STREQ(moved.c_str(), "Original");

    String assigned;
    assigned = std::move(moved);
    EXPECT_TRUE(moved.empty());
    EXPECT_STREQ(assigned.c_str(), "Original");
}

TEST_F(StringTest, CopyTest) {
    String original("Original");
    String copied(original);  // 使用拷贝构造函数

    EXPECT_STREQ(original.c_str(), "Original");
    EXPECT_STREQ(copied.c_str(), "Original");
    EXPECT_NE(original.c_str(), copied.c_str());  // 比较指针地址

    String assigned;
    assigned = original;
    EXPECT_STREQ(assigned.c_str(), "Original");
    EXPECT_NE(original.c_str(), assigned.c_str());
}

/// 测试字符串连接
TEST_F(StringTest, ConcatenationTest) {
    String s1("Hello");
    String s2(" World");

    // 测试 operator+=
    String s3 = s1;
    s3 += s2;
    EXPECT_STREQ(s3.c_str(), "Hello World");

    // 测试 operator+
    String s4 = s1 + s2;
    EXPECT_STREQ(s4.c_str(), "Hello World");

    // 测试与C字符串的连接
    String s5 = s1;
    s5 += " Test";  // 使用 += 操作符
    EXPECT_STREQ(s5.c_str(), "Hello Test");

    String s6 = String("Test ") + s1;  // 显式构造
    EXPECT_STREQ(s6.c_str(), "Test Hello");
}

// 测试比较操作
TEST_F(StringTest, ComparisonTest) {
    String s1("Hello");
    String s2("Hello");
    String s3("World");

    EXPECT_TRUE(s1 == s2);
    EXPECT_FALSE(s1 == s3);
    EXPECT_TRUE(s1 != s3);
    EXPECT_FALSE(s1 != s2);

    EXPECT_TRUE(s1 == "Hello");
    EXPECT_FALSE(s1 == "World");
}

// 测试访问操作
TEST_F(StringTest, AccessTest) {
    String s("Hello");

    EXPECT_EQ(s[0], 'H');
    EXPECT_EQ(s[4], 'o');

    // 测试越界访问
    EXPECT_THROW(s[5], std::out_of_range);

    // 测试const访问
    const String cs("Test");
    EXPECT_EQ(cs[0], 'T');
    EXPECT_THROW(cs[4], std::out_of_range);
}

TEST_F(StringTest, CapacityTest) {
    String s1;
    fmt::print("\nTesting empty string:\n");
    fmt::print("s1: size={}, capacity={}\n", s1.size(), s1.capacity());
    EXPECT_EQ(s1.capacity(), String::SSO_CAPACITY);  // 空字符串也应该有SSO容量

    String s2("Hello");
    fmt::print("\nTesting small string:\n");
    fmt::print("s2: size={}, capacity={}\n", s2.size(), s2.capacity());
    EXPECT_EQ(s2.capacity(), String::SSO_CAPACITY);

    fmt::print("\nTesting reserve:\n");
    s2.reserve(10);
    fmt::print("After reserve(10): size={}, capacity={}\n", s2.size(), s2.capacity());
    EXPECT_EQ(s2.capacity(), String::SSO_CAPACITY);  // 应该保持SSO容量

    String s3;
    s3.reserve(32);
    fmt::print("\nTesting large reserve:\n");
    fmt::print("s3: size={}, capacity={}\n", s3.size(), s3.capacity());
    EXPECT_EQ(s3.capacity(), 32);
}
// 测试清除操作
TEST_F(StringTest, ClearTest) {
    String s("Hello");
    EXPECT_FALSE(s.empty());

    s.clear();
    EXPECT_TRUE(s.empty());
    EXPECT_EQ(s.size(), 0);
    EXPECT_STREQ(s.c_str(), "");
}

// 测试类型转换
TEST_F(StringTest, ConversionTest) {
    String s("Hello");

    std::string stdStr = static_cast<std::string>(s);
    EXPECT_EQ(stdStr, "Hello");

    std::string_view sv = static_cast<std::string_view>(s);
    EXPECT_EQ(sv, "Hello");
}

// 修改 MemoryTest，添加统计信息
TEST_F(StringTest, MemoryTest) {
    StringMemoryPool::getInstance().resetStats();
    fmt::print("\n=== Before Memory Test ===\n");
    StringMemoryPool::getInstance().printStats();

    String s("a");
    for (int i = 1; i < 1000; ++i) {
        s += "a";
    }

    fmt::print("\n=== After 1000 Concatenations ===\n");
    StringMemoryPool::getInstance().printStats();

    EXPECT_EQ(s.size(), 1000);
    EXPECT_GE(s.capacity(), 1000);
}


// 添加一个专门的内存统计测试
TEST_F(StringTest, MemoryPoolStatsTest) {
    // 重置统计信息开始测试
    StringMemoryPool::getInstance().resetStats();

    // 创建不同大小的字符串测试不同的内存池
    String small("Small");  // 应该使用小字符串池
    String medium(std::string(100, 'M'));  // 应该使用中等字符串池
    String large(std::string(200, 'L'));   // 应该使用大字符串池
    String huge(std::string(1000, 'H'));   // 应该使用自定义分配

    // 打印当前状态
    StringMemoryPool::getInstance().printStats();

    // 测试拷贝和移动操作对内存的影响
    {
        String copied = small;
        String moved = std::move(large);
        StringMemoryPool::getInstance().printStats();
    } // 作用域结束，字符串被销毁

    // 打印销毁后的状态
    StringMemoryPool::getInstance().printStats();
}


