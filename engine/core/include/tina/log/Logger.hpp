#pragma once

#include <string>
#include <fstream>
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
#include <condition_variable>

// 调试日志宏
#ifdef TINA_DEBUG_LOGGER
#define LOG_DEBUG(format, ...) \
do { \
if constexpr (true) { \
fmt::print(fg(fmt::color::light_blue), "[DEBUG] {} - " format "\n", __FUNCTION__, ##__VA_ARGS__); \
} \
} while(false)
#else
#define LOG_DEBUG(format, ...)
#endif

namespace Tina
{
    // 定义队列大小
#ifndef TINA_LOG_QUEUE_SIZE
#define TINA_LOG_QUEUE_SIZE (1 << 20)  // 1MB
#endif

    class Logger
    {
    public:
        enum class Level : uint8_t
        {
            Debug = 0,
            Info,
            Warning,
            Error,
            Off
        };

        static Logger& getInstance();
        void init(const std::string& filename, bool truncate = false, bool consoleOutput = true);
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

        template <typename... Args>
        void debug(const char* file, uint32_t line, fmt::format_string<Args...> format, Args&&... args)
        {
            if (isLevelEnabled(Level::Debug))
            {
                logImpl(Level::Debug, file, line, format, std::forward<Args>(args)...);
            }
        }

        template <typename... Args>
        void info(const char* file, uint32_t line,fmt::format_string<Args...> format, Args&&... args)
        {
            if (isLevelEnabled(Level::Info))
            {
                logImpl(Level::Info,file, line, format, std::forward<Args>(args)...);
            }
        }

        template <typename... Args>
        void warning(const char* file, uint32_t line,fmt::format_string<Args...> format, Args&&... args)
        {
            if (isLevelEnabled(Level::Warning))
            {
                logImpl(Level::Warning, file, line,format, std::forward<Args>(args)...);
            }
        }

        template <typename... Args>
        void error(const char* file, uint32_t line,fmt::format_string<Args...> format, Args&&... args)
        {
            if (isLevelEnabled(Level::Error))
            {
                logImpl(Level::Error, file, line,format, std::forward<Args>(args)...);
            }
        }

    private:
        Logger();
        ~Logger();
        Logger(const Logger&) = delete;
        Logger& operator=(const Logger&) = delete;

        struct LogMessage
        {
            Level level;
            std::chrono::system_clock::time_point timestamp;
            std::string content;
            std::string file;
            uint32_t line;

            LogMessage(const Level lvl, const std::chrono::system_clock::time_point ts, const std::string_view msg,
                       const std::string_view filename, const uint32_t lineNumber)
                : level(lvl), timestamp(ts), content(msg), file(filename), line(lineNumber)
            {
            }
        };

        template <typename... Args>
        void logImpl(Level level, const char* file, uint32_t line, fmt::format_string<Args...> format, Args&&... args)
        {
            try
            {
                std::string message = fmt::format(format, std::forward<Args>(args)...);
                auto now = std::chrono::system_clock::now();

                bool needsFlush = level >= flushLevel_;

                {
                    std::lock_guard<std::mutex> lock(queueMutex_);
                    messageQueue_.emplace(level, now, std::move(message), file, line);
                    if (needsFlush)
                    {
                        // 对于高级别的日志，直接处理消息队列
                        processQueuedMessages();
                    }
                    else
                    {
                        // 否则只通知轮询线程
                        queueCV_.notify_one();
                    }
                }
            }
            catch (const std::exception& e)
            {
                LOG_DEBUG("Error in logImpl: {}\n", e.what());
            }
        }

        void processQueuedMessages();

        std::atomic<Level> currentLevel_;
        int64_t flushDelay_;
        uint32_t flushBufferSize_;
        Level flushLevel_;
        std::ofstream outputFile_;
        bool manageFp_;
        bool consoleOutput_; // 控制台输出标志

        std::mutex queueMutex_;
        std::queue<LogMessage> messageQueue_;

        std::atomic<bool> threadRunning_;
        std::thread pollingThread_;
        std::condition_variable queueCV_;
        std::atomic_bool shouldExit_{false};
    };

#define TINA_LOG_DEBUG(...) Tina::Logger::getInstance().debug(__FILE__, __LINE__, __VA_ARGS__)
#define TINA_LOG_INFO(...) Tina::Logger::getInstance().info(__FILE__, __LINE__, __VA_ARGS__)
#define TINA_LOG_WARNING(...) Tina::Logger::getInstance().warning(__FILE__, __LINE__, __VA_ARGS__)
#define TINA_LOG_ERROR(...) Tina::Logger::getInstance().error(__FILE__, __LINE__, __VA_ARGS__)
} // namespace Tina
