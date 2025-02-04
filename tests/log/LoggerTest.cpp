#include "tina/log/Logger.hpp"
#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <filesystem>

namespace fs = std::filesystem;

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
        
        // 启动日志系统
        logger.setOutputFile("test_logs/test.log");
        logger.start();
    }

    void TearDown() override {
        // 停止日志系统
        auto& logger = Tina::Logger::instance();
        if (logger.isRunning()) {
            logger.stop();
        }

        // 等待文件系统操作完成
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // 清理测试目录
        cleanupTestDirectory();
    }

private:
    void cleanupTestDirectory() {
        try {
            if (fs::exists("test_logs")) {
                for (int retry = 0; retry < 3; ++retry) {
                    std::error_code ec;
                    fs::remove_all("test_logs", ec);
                    if (!ec) break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            }
        } catch (const std::exception& e) {
            fmt::print(stderr, "Warning: Failed to cleanup test directory: {}\n", e.what());
        }
    }
};

TEST_F(LoggerTest, BasicLogging) {
    auto& logger = Tina::Logger::instance();
    ASSERT_TRUE(logger.isRunning()) << "Logger should be running";
    
    TINA_LOG_INFO("TestModule", "Basic log message");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    ASSERT_TRUE(fs::exists("test_logs/test.log")) << "Log file should exist";
}

TEST_F(LoggerTest, MultiThreadLogging) {
    auto& logger = Tina::Logger::instance();
    ASSERT_TRUE(logger.isRunning()) << "Logger should be running";
    
    constexpr int kThreadCount = 4;
    constexpr int kMessagesPerThread = 10;  // 减少消息数量以加快测试
    
    std::vector<std::thread> threads;
    for (int i = 0; i < kThreadCount; ++i) {
        threads.emplace_back([i, kMessagesPerThread] {
            for (int j = 0; j < kMessagesPerThread; ++j) {
                TINA_LOG_INFO("ThreadTest", "Thread {} Message {}", i, j);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));  // 添加小延迟
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    // 等待日志写入
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    ASSERT_TRUE(fs::exists("test_logs/test.log")) << "Log file should exist";
    
    std::error_code ec;
    auto fileSize = fs::file_size("test_logs/test.log", ec);
    ASSERT_FALSE(ec) << "Failed to get file size: " << ec.message();
    ASSERT_GT(fileSize, 0) << "Log file should not be empty";
}

TEST_F(LoggerTest, LogLevels) {
    auto& logger = Tina::Logger::instance();
    ASSERT_TRUE(logger.isRunning()) << "Logger should be running";
    
    TINA_LOG_TRACE("TestModule", "Trace message");
    TINA_LOG_DEBUG("TestModule", "Debug message");
    TINA_LOG_INFO("TestModule", "Info message");
    TINA_LOG_WARN("TestModule", "Warning message");
    TINA_LOG_ERROR("TestModule", "Error message");
    TINA_LOG_FATAL("TestModule", "Fatal message");

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    ASSERT_TRUE(fs::exists("test_logs/test.log")) << "Log file should exist";
}
