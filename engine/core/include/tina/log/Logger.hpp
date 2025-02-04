#pragma once
#include <string_view>
#include <fmt/format.h>
#include <fmt/chrono.h>
#include <array>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <memory>

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
    std::chrono::system_clock::time_point timestamp;
    LogLevel level;
    std::string_view module;
    std::string message;
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
    static Logger& instance() {
        static Logger instance;
        return instance;
    }

    template<typename... Args>
    void log(LogLevel level, std::string_view module, fmt::format_string<Args...> fmt, Args&&... args) {
        if (level < minLevel_) return;

        LogMessage msg{
            std::chrono::system_clock::now(),
            level,
            module,
            fmt::format(fmt, std::forward<Args>(args)...)
        };

        if (!messageBuffer_.push(std::move(msg))) {
            // Buffer full, handle overflow
            std::lock_guard<std::mutex> lock(overflowMutex_);
            overflowMessages_.push_back(std::move(msg));
        }
        flushCV_.notify_one();
    }

    void setMinLevel(LogLevel level) { minLevel_ = level; }
    void setOutputFile(const std::string& filename);
    void start();
    void stop();

private:
    Logger();
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void processLogs();
    void flush();

    std::atomic<LogLevel> minLevel_{LogLevel::Info};
    RingBuffer messageBuffer_;
    std::vector<LogMessage> overflowMessages_;
    std::mutex overflowMutex_;
    
    std::unique_ptr<FILE, decltype(&fclose)> file_;
    std::thread workerThread_;
    std::mutex mutex_;
    std::condition_variable flushCV_;
    std::atomic<bool> running_{false};
};

#define TINA_LOG_TRACE(module, ...) \
    Tina::Logger::instance().log(Tina::LogLevel::Trace, module, __VA_ARGS__)
#define TINA_LOG_DEBUG(module, ...) \
    Tina::Logger::instance().log(Tina::LogLevel::Debug, module, __VA_ARGS__)
#define TINA_LOG_INFO(module, ...) \
    Tina::Logger::instance().log(Tina::LogLevel::Info, module, __VA_ARGS__)
#define TINA_LOG_WARN(module, ...) \
    Tina::Logger::instance().log(Tina::LogLevel::Warn, module, __VA_ARGS__)
#define TINA_LOG_ERROR(module, ...) \
    Tina::Logger::instance().log(Tina::LogLevel::Error, module, __VA_ARGS__)
#define TINA_LOG_FATAL(module, ...) \
    Tina::Logger::instance().log(Tina::LogLevel::Fatal, module, __VA_ARGS__)

} // namespace Tina
