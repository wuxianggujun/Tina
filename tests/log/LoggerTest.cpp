#include "tina/log/Logger.hpp"
#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <filesystem>

class LoggerTest : public ::testing::Test {
protected:
    void SetUp() override {
        logger_ = &Tina::Logger::instance();
        
        // 清理之前可能存在的测试目录
        try {
            if (std::filesystem::exists("test_logs")) {
                std::filesystem::remove_all("test_logs");
            }
        } catch (const std::exception& e) {
            fmt::print(stderr, "Error cleaning up test directory: {}\n", e.what());
        }

        // 创建测试目录
        std::filesystem::create_directories("test_logs");
        logger_->setOutputFile("test_logs/test.log");
        logger_->start();
    }

    void TearDown() override {
        if (logger_) {
            logger_->stop();
        }

        // 等待一段时间确保所有文件操作完成
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        try {
            if (std::filesystem::exists("test_logs")) {
                std::filesystem::remove_all("test_logs");
            }
        } catch (const std::exception& e) {
            fmt::print(stderr, "Error cleaning up test directory: {}\n", e.what());
        }
    }

    Tina::Logger* logger_;
};

TEST_F(LoggerTest, BasicLogging) {
    ASSERT_TRUE(logger_ != nullptr);
    
    TINA_LOG_INFO("TestModule", "Basic log message");
    TINA_LOG_ERROR("TestModule", "Error message with number {}", 42);
    
    // 给异步日志一些时间写入
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // 验证日志文件存在
    EXPECT_TRUE(std::filesystem::exists("test_logs/test.log"));
}

TEST_F(LoggerTest, MultiThreadLogging) {
    ASSERT_TRUE(logger_ != nullptr);
    
    constexpr int kThreadCount = 4;
    constexpr int kMessagesPerThread = 100;
    
    std::vector<std::thread> threads;
    for (int i = 0; i < kThreadCount; ++i) {
        threads.emplace_back([i, kMessagesPerThread] {
            for (int j = 0; j < kMessagesPerThread; ++j) {
                TINA_LOG_INFO("ThreadTest", "Thread {} Message {}", i, j);
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    // 给异步日志一些时间写入
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // 验证日志文件存在且大小大于0
    std::error_code ec;
    auto fileSize = std::filesystem::file_size("test_logs/test.log", ec);
    EXPECT_FALSE(ec) << "Error getting file size: " << ec.message();
    if (!ec) {
        EXPECT_GT(fileSize, 0);
    }
}

TEST_F(LoggerTest, LogLevels) {
    ASSERT_TRUE(logger_ != nullptr);
    
    TINA_LOG_TRACE("TestModule", "Trace message");
    TINA_LOG_DEBUG("TestModule", "Debug message");
    TINA_LOG_INFO("TestModule", "Info message");
    TINA_LOG_WARN("TestModule", "Warning message");
    TINA_LOG_ERROR("TestModule", "Error message");
    TINA_LOG_FATAL("TestModule", "Fatal message");

    // 给异步日志一些时间写入
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // 验证日志文件存在
    EXPECT_TRUE(std::filesystem::exists("test_logs/test.log"));
}
