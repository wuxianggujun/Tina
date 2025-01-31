#include <gtest/gtest.h>
#include <benchmark/benchmark.h>
#include "tina/container/String.hpp"
#include <string>
#include <chrono>

using namespace tina::container;

// 功能测试
class StringTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 测试前的设置
    }

    void TearDown() override {
        // 测试后的清理
    }
};

// 构造函数测试
TEST_F(StringTest, ConstructorTest) {
    // 默认构造函数
    String s1;
    EXPECT_TRUE(s1.empty());
    EXPECT_EQ(s1.length(), 0);

    // C字符串构造函数
    String s2("Hello");
    EXPECT_EQ(s2.length(), 5);
    EXPECT_STREQ(s2.c_str(), "Hello");

    // 拷贝构造函数
    String s3(s2);
    EXPECT_EQ(s3.length(), 5);
    EXPECT_STREQ(s3.c_str(), "Hello");

    // 移动构造函数
    String s4(std::move(s3));
    EXPECT_EQ(s4.length(), 5);
    EXPECT_STREQ(s4.c_str(), "Hello");
}

// 基本操作测试
TEST_F(StringTest, BasicOperationsTest) {
    String str("Hello");
    
    // 长度和容量
    EXPECT_EQ(str.length(), 5);
    EXPECT_FALSE(str.empty());
    
    // 访问操作符
    EXPECT_EQ(str[0], 'H');
    EXPECT_EQ(str[4], 'o');
    
    // at函数（带边界检查）
    EXPECT_EQ(str.at(0), 'H');
    EXPECT_THROW(str.at(5), std::out_of_range);
}

// 字符串操作测试
TEST_F(StringTest, StringOperationsTest) {
    String str1("Hello");
    String str2(" World");
    
    // 追加操作
    str1.append(str2);
    EXPECT_STREQ(str1.c_str(), "Hello World");
    
    // 子字符串
    String sub = str1.substr(0, 5);
    EXPECT_STREQ(sub.c_str(), "Hello");
    
    // 查找
    EXPECT_EQ(str1.find("World"), 6);
    EXPECT_EQ(str1.find("xyz"), String::npos);
}

// 运算符测试
TEST_F(StringTest, OperatorTest) {
    String str1("Hello");
    String str2(" World");
    
    // += 运算符
    str1 += str2;
    EXPECT_STREQ(str1.c_str(), "Hello World");
    
    // + 运算符
    String str3 = String("Hello") + String(" World");
    EXPECT_STREQ(str3.c_str(), "Hello World");
    
    // 比较运算符
    EXPECT_TRUE(String("Hello") == String("Hello"));
    EXPECT_FALSE(String("Hello") == String("World"));
    EXPECT_TRUE(String("Hello") != String("World"));
}

// 性能测试
static void BM_StringCreation(benchmark::State& state) {
    for (auto _ : state) {
        String str("Hello, World!");
        benchmark::DoNotOptimize(str);
    }
}
BENCHMARK(BM_StringCreation);

static void BM_StringAppend(benchmark::State& state) {
    for (auto _ : state) {
        String str;
        for (int i = 0; i < state.range(0); ++i) {
            str.append("a");
        }
        benchmark::DoNotOptimize(str);
    }
}
BENCHMARK(BM_StringAppend)->Range(8, 8<<10);

static void BM_StringConcatenation(benchmark::State& state) {
    String str1("Hello");
    String str2(" World");
    for (auto _ : state) {
        String result = str1 + str2;
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(BM_StringConcatenation);

// 与std::string比较性能
static void BM_CompareWithStdString(benchmark::State& state) {
    const char* test_str = "Hello, World! This is a test string for performance comparison.";
    
    for (auto _ : state) {
        state.PauseTiming(); // 暂停计时器
        // 准备测试数据
        std::vector<String> tina_strings;
        std::vector<std::string> std_strings;
        for (int i = 0; i < 1000; ++i) {
            tina_strings.emplace_back(test_str);
            std_strings.emplace_back(test_str);
        }
        state.ResumeTiming(); // 恢复计时器
        
        // 测试String性能
        for (auto& str : tina_strings) {
            str.append("_test");
            benchmark::DoNotOptimize(str);
        }
        
        state.PauseTiming();
        // 测试std::string性能
        for (auto& str : std_strings) {
            str.append("_test");
            benchmark::DoNotOptimize(str);
        }
        state.ResumeTiming();
    }
}
BENCHMARK(BM_CompareWithStdString);

// 主函数
int main(int argc, char** argv) {
    // 初始化Google Test
    testing::InitGoogleTest(&argc, argv);
    
    // 运行所有测试
    int testResult = RUN_ALL_TESTS();
    
    // 运行性能测试
    ::benchmark::Initialize(&argc, argv);
    ::benchmark::RunSpecifiedBenchmarks();
    
    return testResult;
} 