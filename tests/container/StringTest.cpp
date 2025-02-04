// tests/container/StringTest.cpp
#include <gtest/gtest.h>
#include "tina/container/String.hpp"

class StringTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

// 构造函数测试
TEST_F(StringTest, DefaultConstructor) {
    tina::String str;
    EXPECT_TRUE(str.empty());
    EXPECT_EQ(str.size(), 0);
    EXPECT_STREQ(str.c_str(), "");
}

TEST_F(StringTest, CStringConstructor) {
    tina::String str("Hello");
    EXPECT_FALSE(str.empty());
    EXPECT_EQ(str.size(), 5);
    EXPECT_STREQ(str.c_str(), "Hello");
}

TEST_F(StringTest, CopyConstructor) {
    tina::String str1("Hello");
    tina::String str2(str1);
    EXPECT_STREQ(str2.c_str(), "Hello");
    EXPECT_EQ(str2.size(), str1.size());
}

// SSO测试
TEST_F(StringTest, SSOTest) {
    tina::String short_str("Short");
    tina::String long_str("This is a very long string that will not fit in SSO buffer");
    
    EXPECT_STREQ(short_str.c_str(), "Short");
    EXPECT_STREQ(long_str.c_str(), "This is a very long string that will not fit in SSO buffer");
}

// 赋值操作符测试
TEST_F(StringTest, AssignmentOperator) {
    tina::String str1("Hello");
    tina::String str2;
    str2 = str1;
    EXPECT_STREQ(str2.c_str(), "Hello");
    
    str2 = "World";
    EXPECT_STREQ(str2.c_str(), "World");
}

// 追加操作测试
TEST_F(StringTest, Append) {
    tina::String str("Hello");
    str.append(" World");
    EXPECT_STREQ(str.c_str(), "Hello World");
    
    str += "!";
    EXPECT_STREQ(str.c_str(), "Hello World!");
}

// 查找操作测试
TEST_F(StringTest, Find) {
    tina::String str("Hello World");
    EXPECT_EQ(str.find("World"), 6);
    EXPECT_EQ(str.find('o'), 4);
    EXPECT_EQ(str.find("xyz"), tina::String::npos);
}

// 子串操作测试
TEST_F(StringTest, Substr) {
    tina::String str("Hello World");
    tina::String sub = str.substr(6, 5);
    EXPECT_STREQ(sub.c_str(), "World");
}

// 比较操作测试
TEST_F(StringTest, Compare) {
    tina::String str1("Hello");
    tina::String str2("Hello");
    tina::String str3("World");
    
    EXPECT_TRUE(str1 == str2);
    EXPECT_FALSE(str1 == str3);
    EXPECT_TRUE(str1 < str3);
}

// 容量操作测试
TEST_F(StringTest, Capacity) {
    tina::String str;
    EXPECT_EQ(str.capacity(), 15); // SSO容量
    
    str.reserve(100);
    EXPECT_GE(str.capacity(), 100);
    
    str = "Hello";
    str.shrink_to_fit();
    EXPECT_EQ(str.capacity(), 15); // 回到SSO
}

// 异常测试
TEST_F(StringTest, Exceptions) {
    tina::String str("Hello");
    EXPECT_THROW(str.at(10), std::out_of_range);
    EXPECT_THROW(str.substr(10), std::out_of_range);
}

// 连接操作测试
TEST_F(StringTest, Concatenation) {
    tina::String str1("Hello");
    tina::String str2(" World");
    tina::String str3 = str1 + str2;
    EXPECT_STREQ(str3.c_str(), "Hello World");
    
    str3 = str1 + " Test";
    EXPECT_STREQ(str3.c_str(), "Hello Test");
    
    str3 = "Test " + str2;
    EXPECT_STREQ(str3.c_str(), "Test  World");
}

// 移动语义测试
TEST_F(StringTest, MoveSemantics) {
    tina::String str1("Hello World");
    tina::String str2(std::move(str1));
    
    EXPECT_STREQ(str2.c_str(), "Hello World");
    EXPECT_TRUE(str1.empty()); // 移动后原字符串应为空
    
    tina::String str3;
    str3 = std::move(str2);
    EXPECT_STREQ(str3.c_str(), "Hello World");
    EXPECT_TRUE(str2.empty());
}

// 大数据SIMD优化测试
TEST_F(StringTest, SIMDOptimization) {
    const size_t size = 1024;
    std::string long_data(size, 'A');
    
    tina::String str1(long_data.c_str());
    tina::String str2;
    
    // 测试SIMD优化的复制性能
    auto start = std::chrono::high_resolution_clock::now();
    str2 = str1;
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    EXPECT_EQ(str2.size(), size);
    EXPECT_TRUE(str2 == str1);
    // 在现代处理器上，1024字节的SIMD复制应该在几微秒内完成
    EXPECT_LT(duration.count(), 100); // 100微秒是一个保守的上限
}

// 边界情况测试
TEST_F(StringTest, EdgeCases) {
    // 空字符串操作
    tina::String empty_str;
    tina::String temp1;
    temp1 = empty_str + "";
    temp1 = empty_str.append("");
    EXPECT_EQ(empty_str.find(""), 0);
    
    // 极限长度字符串
    std::string very_long_data(1000000, 'X');
    EXPECT_NO_THROW({
        tina::String long_str(very_long_data.c_str());
        EXPECT_EQ(long_str.size(), 1000000);
    });
    
    // 特殊字符
    tina::String special_chars("\\n\\t\\0\\r");
    EXPECT_EQ(special_chars.size(), 8);
    
    // 重复字符
    tina::String repeated("aaaaaaaaaaaaaaa"); // 15字符，刚好是SSO边界
    EXPECT_EQ(repeated.size(), 15);
    repeated += "a"; // 触发堆分配
    EXPECT_EQ(repeated.size(), 16);
}

// 内存管理测试
TEST_F(StringTest, MemoryManagement) {
    tina::String str;
    
    // 测试多次重新分配
    for(int i = 0; i < 100; ++i) {
        str += "test";
    }
    
    size_t capacity_before = str.capacity();
    str.clear();
    EXPECT_EQ(str.size(), 0);
    EXPECT_EQ(str.capacity(), capacity_before); // clear不应改变容量
    
    str.shrink_to_fit();
    EXPECT_EQ(str.capacity(), 15); // 应该回到SSO大小
    
    // 测试精确容量控制
    str.reserve(1000);
    EXPECT_GE(str.capacity(), 1000);
    str = "small";
    str.shrink_to_fit();
    EXPECT_EQ(str.capacity(), 15); // 小字符串应该使用SSO
}

// 迭代器测试
TEST_F(StringTest, Iterators) {
    tina::String str("Hello");
    std::string result;
    
    // 正向迭代
    for (auto it = str.begin(); it != str.end(); ++it) {
        result += *it;
    }
    EXPECT_EQ(result, "Hello");
    
    // const迭代器
    const tina::String const_str("World");
    result.clear();
    for (auto it = const_str.begin(); it != const_str.end(); ++it) {
        result += *it;
    }
    EXPECT_EQ(result, "World");
    
    // 范围for循环
    result.clear();
    for (char c : str) {
        result += c;
    }
    EXPECT_EQ(result, "Hello");
    
    // 空字符串迭代
    tina::String empty_str;
    EXPECT_EQ(empty_str.begin(), empty_str.end());
    
    // cbegin/cend测试
    result.clear();
    for (auto it = str.cbegin(); it != str.cend(); ++it) {
        result += *it;
    }
    EXPECT_EQ(result, "Hello");
}