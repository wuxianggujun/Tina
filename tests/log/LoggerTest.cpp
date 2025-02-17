#include "tina/log/Logger.hpp"
#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include "tina/core/filesystem.hpp"

namespace fs = ghc::filesystem;

class LoggerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 确保日志系统处于已停止状态
        auto& logger = Tina::Logger::instance();
        if (logger.isRunning()) {
            logger.stop();
        }

        // 清理测试目录
        cleanupTestDirectory();
        
        // 创建新的测试目录
        fs::create_directories("test_logs");
        
        // 获取日志文件的绝对路径
        fs::path logPath = fs::absolute("test_logs/test.log");
        fmt::print("Log file path: {}\n", logPath.string());
        
        // 启动日志系统
        logger.setOutputFile(Tina::String(logPath.string()));
        logger.start();
    }

    void TearDown() override {
        // 停止日志系统
        auto& logger = Tina::Logger::instance();
        logger.stop();

        // 清理测试目录
        cleanupTestDirectory();
    }

private:
    void cleanupTestDirectory() {
        try {
            fs::remove_all("test_logs");
        } catch (const std::exception& e) {
            fmt::print(stderr, "Failed to cleanup test directory: {}\n", e.what());
        }
    }
};

TEST_F(LoggerTest, BasicLogging) {
    auto& logger = Tina::Logger::instance();
    
    // 使用 String 类型进行日志记录
    TINA_CORE_LOG_INFO("TestModule", "This is a test message");
    TINA_CORE_LOG_ERROR("TestModule", "This is an error message");
    
    // 给一些时间让异步日志完成写入
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // 验证日志文件是否存在
    EXPECT_TRUE(fs::exists("test_logs/test.log"));
    
    // 打印日志文件的大小
    auto size = fs::file_size("test_logs/test.log");
    fmt::print("Log file size: {} bytes\n", size);
}

TEST_F(LoggerTest, MultiThreadLogging) {
    auto& logger = Tina::Logger::instance();
    
    constexpr int NUM_THREADS = 4;
    constexpr int MESSAGES_PER_THREAD = 100;
    
    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back([i, MESSAGES_PER_THREAD]() {
            for (int j = 0; j < MESSAGES_PER_THREAD; ++j) {
                TINA_CORE_LOG_INFO("ThreadTest", 
                    "Message {} from thread {}", j, i);
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    // 给一些时间让异步日志完成写入
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // 验证日志文件存在并且大小不为0
    EXPECT_TRUE(fs::exists("test_logs/test.log"));
    EXPECT_GT(fs::file_size("test_logs/test.log"), 0);
}

TEST_F(LoggerTest, LogLevels) {
    auto& logger = Tina::Logger::instance();
    
    // 设置最小日志级别为 INFO
    logger.setMinLevel(Tina::LogLevel::Info);
    
    // Debug 级别的消息不应该被记录
    TINA_CORE_LOG_DEBUG("TestModule", "This debug message should not appear");
    
    // Info 及以上级别的消息应该被记录
    TINA_CORE_LOG_INFO("TestModule", "This info message should appear");
    TINA_CORE_LOG_WARN("TestModule", "This warning message should appear");
    TINA_CORE_LOG_ERROR("TestModule", "This error message should appear");
    
    // 给一些时间让异步日志完成写入
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}
