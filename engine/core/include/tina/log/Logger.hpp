#pragma once
#include <fmt/format.h>
#include <fmt/chrono.h>
#include "tina/core/Core.hpp"
#include "tina/container/String.hpp"
#include "tina/core/filesystem.hpp"

#include <thread>
#include <mutex>
#include <condition_variable>

namespace Tina {

enum class LogLevel : uint8_t {
    Trace,
    Debug,
    Info,
    Warn,
    Error,
    Fatal,
    Off
};

struct LogMessage {
    LogMessage() : timestamp(std::chrono::system_clock::now()), level(LogLevel::Info) {}
    
    LogMessage(std::chrono::system_clock::time_point ts, LogLevel lvl, String mod, String msg)
        : timestamp(ts)
        , level(lvl)
        , module(std::move(mod))
        , message(std::move(msg)) {}
        
    std::chrono::system_clock::time_point timestamp;
    LogLevel level;
    String module;
    String message;
};

class RingBuffer {
public:
    explicit RingBuffer(size_t capacity = 8192) 
        : buffer_(capacity)
        , capacity_(capacity)
        , head_(0)
        , tail_(0) {}

    bool push(LogMessage&& msg) {
        size_t next = (head_ + 1) % capacity_;
        if (next == tail_) return false;  // Buffer is full
        buffer_[head_] = std::move(msg);
        head_ = next;
        return true;
    }

    bool pop(LogMessage& msg) {
        if (tail_ == head_) return false;  // Buffer is empty
        msg = std::move(buffer_[tail_]);
        tail_ = (tail_ + 1) % capacity_;
        return true;
    }

private:
    std::vector<LogMessage> buffer_;
    const size_t capacity_;
    std::atomic<size_t> head_;
    std::atomic<size_t> tail_;
};

class Logger {
public:
    // 单例访问
    static Logger& instance() {
        static Logger instance;
        return instance;
    }

    // 删除的函数放在public部分
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    Logger(Logger&&) = delete;
    Logger& operator=(Logger&&) = delete;

    template<typename... Args>
    void log(LogLevel level, std::string_view module, fmt::format_string<Args...> fmt, Args&&... args) {
        if (level < minLevel_) return;
        log(level, module, fmt::format(fmt, std::forward<Args>(args)...));
    }

    void log(LogLevel level, std::string_view module, std::string_view message);

    void setMinLevel(LogLevel level) { minLevel_ = level; }
    void setOutputFile(const String& filename);
    void start();
    void stop();
    bool isRunning() const { return running_; }

private:
    // 构造函数和析构函数
    Logger() : minLevel_(LogLevel::Info), logFile_(nullptr), running_(false) {
        fmt::print("Logger instance created\n");
    }
    ~Logger();

    void processLogs();
    void flush();

    std::atomic<LogLevel> minLevel_;
    RingBuffer messageBuffer_;
    std::vector<LogMessage> overflowMessages_;
    std::mutex overflowMutex_;
    
    FILE* logFile_;
    std::thread workerThread_;
    std::mutex mutex_;
    std::condition_variable flushCV_;
    std::atomic<bool> running_;
    ghc::filesystem::path logPath_;
};

#define TINA_LOG_TRACE(module, ...) do { \
    if (::Tina::Logger::instance().isRunning()) \
        ::Tina::Logger::instance().log(::Tina::LogLevel::Trace, module, __VA_ARGS__); \
} while(0)

#define TINA_LOG_DEBUG(module, ...) do { \
    if (::Tina::Logger::instance().isRunning()) \
        ::Tina::Logger::instance().log(::Tina::LogLevel::Debug, module, __VA_ARGS__); \
} while(0)

#define TINA_LOG_INFO(module, ...) do { \
    if (::Tina::Logger::instance().isRunning()) \
        ::Tina::Logger::instance().log(::Tina::LogLevel::Info, module, __VA_ARGS__); \
} while(0)

#define TINA_LOG_WARN(module, ...) do { \
    if (::Tina::Logger::instance().isRunning()) \
        ::Tina::Logger::instance().log(::Tina::LogLevel::Warn, module, __VA_ARGS__); \
} while(0)

#define TINA_LOG_ERROR(module, ...) do { \
    if (::Tina::Logger::instance().isRunning()) \
        ::Tina::Logger::instance().log(::Tina::LogLevel::Error, module, __VA_ARGS__); \
} while(0)

#define TINA_LOG_FATAL(module, ...) do { \
    if (::Tina::Logger::instance().isRunning()) \
        ::Tina::Logger::instance().log(::Tina::LogLevel::Fatal, module, __VA_ARGS__); \
} while(0)

} // namespace Tina
