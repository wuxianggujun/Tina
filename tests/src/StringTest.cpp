//
// Created by wuxianggujun on 2025/1/26.
//
#include <gtest/gtest.h>
#include "base/String.hpp"

using namespace Tina;

class StringTest:public testing::Test
{
protected:
    void SetUp() override{}
    void TearDown() override{}
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

// 测试容量管理
TEST_F(StringTest, CapacityTest) {
    String s;
    EXPECT_EQ(s.capacity(), 0);

    s.reserve(10);
    EXPECT_GE(s.capacity(), 10);

    String s2("Hello");
    size_t originalCapacity = s2.capacity();
    s2 += " World";  // 应该触发自动扩容
    EXPECT_GT(s2.capacity(), originalCapacity);
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

// 修改内存测试
TEST_F(StringTest, MemoryTest) {
    String s("a");
    for (int i = 1; i < 1000; ++i) {
        s += "a";  // 使用 += 操作符
    }
    EXPECT_EQ(s.size(), 1000);
    EXPECT_GE(s.capacity(), 1000);
}