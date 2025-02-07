#pragma once

#include <string>
#include <memory>
#include <fmt/format.h>
#include <fmt/chrono.h>
#include <fmt/ranges.h>
#include <fmt/color.h>
#include <chrono>
#include <atomic>
#include <thread>
#include <mutex>
#include <queue>
#include <vector>

// 调试日志宏
#ifdef _DEBUG
#define LOG_DEBUG(format, ...) fmt::print(fg(fmt::color::light_blue), "[DEBUG] {} - " format "\n", __FUNCTION__, ##__VA_ARGS__)
#else
#define LOG_DEBUG(format, ...)
#endif

namespace Tina {

// 定义队列大小
#ifndef TINA_LOG_QUEUE_SIZE
#define TINA_LOG_QUEUE_SIZE (1 << 20)  // 1MB
#endif

class Logger {
public:
    enum class Level : uint8_t {
        Debug = 0,
        Info,
        Warning,
        Error,
        Off
    };

    static Logger& getInstance();
    void init(const std::string& filename, bool truncate = false);
    void init(FILE* fp, bool manageFp = false);
    void setLogLevel(Level level);
    Level getLogLevel() const;
    bool isLevelEnabled(Level level) const;
    void setFlushDelay(int64_t ns);
    void setFlushLevel(Level level);
    void setFlushBufferSize(uint32_t bytes);
    void startPollingThread(int64_t pollInterval = 1000000000);
    void stopPollingThread();
    void flush(bool force = false);
    void close();

    template<typename... Args>
    void debug(fmt::format_string<Args...> format, Args&&... args) {
        if (isLevelEnabled(Level::Debug)) {
            logImpl(Level::Debug, format, std::forward<Args>(args)...);
        }
    }

    template<typename... Args>
    void info(fmt::format_string<Args...> format, Args&&... args) {
        if (isLevelEnabled(Level::Info)) {
            logImpl(Level::Info, format, std::forward<Args>(args)...);
        }
    }

    template<typename... Args>
    void warning(fmt::format_string<Args...> format, Args&&... args) {
        if (isLevelEnabled(Level::Warning)) {
            logImpl(Level::Warning, format, std::forward<Args>(args)...);
        }
    }

    template<typename... Args>
    void error(fmt::format_string<Args...> format, Args&&... args) {
        if (isLevelEnabled(Level::Error)) {
            logImpl(Level::Error, format, std::forward<Args>(args)...);
        }
    }

private:
    Logger();
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    struct LogMessage {
        Level level;
        std::chrono::system_clock::time_point timestamp;
        std::string content;
        
        LogMessage(Level lvl, std::chrono::system_clock::time_point ts, std::string&& msg)
            : level(lvl), timestamp(ts), content(std::move(msg)) {}
    };

    template<typename... Args>
    void logImpl(Level level, fmt::format_string<Args...> format, Args&&... args) {
        LOG_DEBUG("Formatting message");
        std::string message = fmt::format(format, std::forward<Args>(args)...);
        
        LOG_DEBUG("Creating timestamp");
        auto now = std::chrono::system_clock::now();
            
        LOG_DEBUG("Acquiring mutex");
        std::unique_lock<std::mutex> lock(queueMutex_);
        LOG_DEBUG("Adding message to queue");
        messageQueue_.emplace(level, now, std::move(message));
        
        if (level >= flushLevel_ || messageQueue_.size() >= flushBufferSize_) {
            LOG_DEBUG("Immediate flush required");
            flush(true);
        }
        LOG_DEBUG("Message queued successfully");
    }

    void processQueuedMessages();

    std::atomic<Level> currentLevel_;
    int64_t flushDelay_;
    uint32_t flushBufferSize_;
    Level flushLevel_;
    FILE* outputFp_;
    bool manageFp_;
    
    std::mutex queueMutex_;
    std::queue<LogMessage> messageQueue_;
    
    std::atomic<bool> threadRunning_;
    std::thread pollingThread_;
    std::condition_variable queueCV_;
    bool shouldExit_ = false;
};

#define TINA_LOG_DEBUG(...) Tina::Logger::getInstance().debug(__VA_ARGS__)
#define TINA_LOG_INFO(...) Tina::Logger::getInstance().info(__VA_ARGS__)
#define TINA_LOG_WARNING(...) Tina::Logger::getInstance().warning(__VA_ARGS__)
#define TINA_LOG_ERROR(...) Tina::Logger::getInstance().error(__VA_ARGS__)

} // namespace Tina
