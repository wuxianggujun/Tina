#include "tina/log/Logger.hpp"
#include <iomanip>

namespace Tina
{
    Logger& Logger::getInstance()
    {
        LOG_DEBUG("Getting logger instance\n");
        static Logger instance;
        return instance;
    }

    Logger::Logger()
        : currentLevel_(Level::Info)
          , flushDelay_(3000000000) // 3秒
          , flushBufferSize_(8 * 1024) // 8KB
          , flushLevel_(Level::Error)
          , manageFp_(false)
          , threadRunning_(false)
          , consoleOutput_(true)
    {
        LOG_DEBUG("Logger constructed\n");
    }

    Logger::~Logger()
    {
        try
        {
            close();
        }
        catch (...)
        {
            // 析构函数不应该抛出异常
        }
    }

    void Logger::init(const std::string& filename, bool truncate, bool consoleOutput)
    {
        try
        {
            if (threadRunning_)
            {
                close();
            }

            {
                std::lock_guard<std::mutex> lock(queueMutex_);
                outputFile_.open(filename, truncate ? std::ios::trunc : std::ios::app);
                if (!outputFile_.is_open())
                {
                    throw std::runtime_error("Failed to open log file: " + filename);
                }
                manageFp_ = true;
                consoleOutput_ = consoleOutput;
            }
            // 自动启动轮询线程，使用100ms的默认轮询间隔
            startPollingThread(100'000'000);
            LOG_DEBUG("Logger initialized with file: {}\n", filename);
        }
        catch (const std::exception& e)
        {
            LOG_DEBUG("Error in init: {}\n", e.what());
            throw;
        }
    }

    void Logger::setLogLevel(Level level)
    {
        LOG_DEBUG("Setting log level to: {}\n", static_cast<int>(level));
        currentLevel_.store(level, std::memory_order_relaxed);
    }

    Logger::Level Logger::getLogLevel() const
    {
        auto level = currentLevel_.load(std::memory_order_relaxed);
        LOG_DEBUG("Current log level: {}\n", static_cast<int>(level));
        return level;
    }

    bool Logger::isLevelEnabled(Level level) const
    {
        bool enabled = level >= currentLevel_.load(std::memory_order_relaxed);
        LOG_DEBUG("Checking if level {} is enabled: {}\n", static_cast<int>(level), enabled);
        return enabled;
    }

    void Logger::setFlushDelay(int64_t ns)
    {
        LOG_DEBUG("Setting flush delay to: {} ns\n", ns);
        flushDelay_ = ns;
    }

    void Logger::setFlushLevel(Level level)
    {
        LOG_DEBUG("Setting flush level to: {}\n", static_cast<int>(level));
        flushLevel_ = level;
    }

    void Logger::setFlushBufferSize(uint32_t bytes)
    {
        LOG_DEBUG("Setting flush buffer size to: {} bytes\n", bytes);
        flushBufferSize_ = bytes;
    }

    void Logger::startPollingThread(int64_t pollInterval)
    {
        try
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            if (threadRunning_)
            {
                return;
            }

            shouldExit_ = false;
            threadRunning_ = true;

            pollingThread_ = std::thread([this, pollInterval]()
            {
                while (!shouldExit_)
                {
                    try
                    {
                        std::unique_lock<std::mutex> lock(queueMutex_);
                        // 等待新消息或退出信号
                        auto result = queueCV_.wait_for(lock,
                                                        std::chrono::nanoseconds(pollInterval),
                                                        [this] { return !messageQueue_.empty() || shouldExit_; });

                        if (result && !messageQueue_.empty())
                        {
                            lock.unlock();
                            processQueuedMessages();
                        }
                    }
                    catch (const std::exception& e)
                    {
                        LOG_DEBUG("Error in polling thread: {}\n", e.what());
                        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // 稍作等待，避免频繁出错
                    }
                }
                LOG_DEBUG("Polling thread stopped\n");
            });

            LOG_DEBUG("Polling thread started\n");
        }
        catch (const std::exception& e)
        {
            threadRunning_ = false;
            LOG_DEBUG("Error starting polling thread: {}\n", e.what());
            throw;
        }
    }

    void Logger::stopPollingThread()
    {
        try
        {
            {
                std::lock_guard<std::mutex> lock(queueMutex_);
                if (!threadRunning_)
                {
                    return;
                }
                shouldExit_ = true;
                queueCV_.notify_one();
            }

            if (pollingThread_.joinable())
            {
                pollingThread_.join();
            }

            threadRunning_ = false;
            LOG_DEBUG("Polling thread stopped successfully\n");
        }
        catch (const std::exception& e)
        {
            LOG_DEBUG("Error stopping polling thread: {}\n", e.what());
            throw;
        }
    }

    void Logger::close()
    {
        LOG_DEBUG("Closing logger\n");
        try
        {
            if (threadRunning_)
            {
                flush(true);
                stopPollingThread();
            }

            {
                std::lock_guard<std::mutex> lock(queueMutex_);
                if (outputFile_.is_open())
                {
                    outputFile_.close();
                }
            }
            manageFp_ = false;

            LOG_DEBUG("Logger closed successfully\n");
        }
        catch (const std::exception& e)
        {
            LOG_DEBUG("Error closing logger: {}\n", e.what());
            throw;
        }
    }

    std::string getBaseName(const std::string& path) {
        size_t lastSlash = path.find_last_of("/\\");
        if (lastSlash != std::string::npos) {
            return path.substr(lastSlash + 1);
        }
        return path;
    }

    static const char* getBaseName(const char* path) {
        const char* lastSlash = std::strrchr(path, '/');
        const char* lastBackslash = std::strrchr(path, '\\');
        const char* last = (lastSlash > lastBackslash) ? lastSlash : lastBackslash;
        return last ? last + 1 : path;
    }

    void Logger::processQueuedMessages()
    {
        std::vector<LogMessage> localQueue;

        // 首先获取消息队列的副本
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            if (messageQueue_.empty())
            {
                return;
            }

            localQueue.reserve(messageQueue_.size());
            while (!messageQueue_.empty())
            {
                localQueue.push_back(std::move(messageQueue_.front()));
                messageQueue_.pop();
            }
        }

        static const char* levelStrings[] = {
            "TRACE", "DEBUG", "INFO", "WARN", "ERROR"
        };

        static constexpr fmt::color levelColors[] = {
            fmt::color::gray,      // TRACE
            fmt::color::light_blue,// DEBUG
            fmt::color::light_green,// INFO
            fmt::color::yellow,    // WARN
            fmt::color::red       // ERROR
        };

        std::string buffer;
        buffer.reserve(localQueue.size() * 64);
        size_t processedCount = 0;
        for (const auto& msg : localQueue)
        {
            try
            {
                // 格式化时间戳
                auto time_t = std::chrono::system_clock::to_time_t(msg.timestamp);
                auto tm = *std::localtime(&time_t);
                char timestamp[32];
                std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &tm);

                std::string logLine = fmt::format("[{}] [{}] [{}:{}] {}\n",
                                timestamp,
                                levelStrings[static_cast<size_t>(msg.level)],
                                getBaseName(msg.file.c_str()),
                                msg.line,
                                msg.content);  // 这里添加了消息内容

                buffer += logLine;

                // 控制台输出（带颜色）
                if (consoleOutput_)
                {
                    fmt::color color = levelColors[static_cast<size_t>(msg.level)];
                    fmt::print(fg(color), "{}", logLine);
                    fflush(stdout); // 强制刷新标准输出
                }

                processedCount++;
            }
            catch (const std::exception& e)
            {
                LOG_DEBUG("Error processing log message: {}\n", e.what());
            }
        }

        // 批量写入文件
        if (!buffer.empty())
        {
            try
            {
                std::lock_guard<std::mutex> lock(queueMutex_);
                if (outputFile_.is_open())
                {
                    outputFile_.write(buffer.c_str(), buffer.size());
                    if (localQueue.back().level>= flushLevel_)
                    {
                        outputFile_.flush();
                    }
                }
            }
            catch (const std::exception& e)
            {
                LOG_DEBUG("Error writing to log file: {}\n", e.what());
            }
        }
         if (processedCount > 0)
        {
            LOG_DEBUG("Processed {} messages\n", processedCount);
        }
    }

      void Logger::flush(const bool force)
    {
        if (!threadRunning_ && !force) {
            return;
        }

        try
        {
            if (force)
            {
                 processQueuedMessages(); //强制刷新
            }
            else
            {
                queueCV_.notify_one();  // 非强制刷新只通知轮询线程
            }
        }
        catch (const std::exception& e)
        {
            LOG_DEBUG("Error in flush: {}\n", e.what());
        }
    }
}