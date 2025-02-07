#include "tina/log/Logger.hpp"
#include <iomanip>
#include <sstream>

namespace Tina {

Logger& Logger::getInstance() {
    LOG_DEBUG("Getting logger instance");
    static Logger instance;
    return instance;
}

Logger::Logger()
    : currentLevel_(Level::Info)
    , flushDelay_(3000000000) // 3秒
    , flushBufferSize_(8 * 1024) // 8KB
    , flushLevel_(Level::Error)
    , outputFp_(nullptr)
    , manageFp_(false)
    , threadRunning_(false)
    , shouldExit_(false) {
    LOG_DEBUG("Logger constructed");
}

Logger::~Logger() {
    LOG_DEBUG("Logger destructor called");
    stopPollingThread();
    flush(true);
    close();
    LOG_DEBUG("Logger destroyed");
}

void Logger::init(const std::string& filename, bool truncate) {
    LOG_DEBUG("Initializing logger with file: {}", filename);
    try {
	    if (threadRunning_)
	    {
            close();
	    }

        std::lock_guard<std::mutex> lock(queueMutex_);

        outputFp_ = fopen(filename.c_str(), truncate ? "w" : "a");
        if (!outputFp_) {
            LOG_DEBUG("Failed to open file: {}", filename);
            throw std::runtime_error("Failed to open log file");
        }
        manageFp_ = true;
        LOG_DEBUG("Logger initialized with file successfully");
    } catch (const std::exception& e) {
        LOG_DEBUG("Exception in init: {}", e.what());
        throw;
    }
}

void Logger::init(FILE* fp, bool manageFp) {
    LOG_DEBUG("Initializing logger with FILE pointer");
    try {
        if (threadRunning_)
        {
            close();
        }

        std::lock_guard<std::mutex> lock(queueMutex_);

        outputFp_ = fp;
        manageFp_ = manageFp;
        LOG_DEBUG("Logger initialized with FILE pointer successfully");
    } catch (const std::exception& e) {
        LOG_DEBUG("Exception in init: {}", e.what());
        throw;
    }
}

void Logger::setLogLevel(Level level) {
    LOG_DEBUG("Setting log level to: {}", static_cast<int>(level));
    currentLevel_.store(level, std::memory_order_relaxed);
}

Logger::Level Logger::getLogLevel() const {
    auto level = currentLevel_.load(std::memory_order_relaxed);
    LOG_DEBUG("Current log level: {}", static_cast<int>(level));
    return level;
}

bool Logger::isLevelEnabled(Level level) const {
    bool enabled = level >= currentLevel_.load(std::memory_order_relaxed);
    LOG_DEBUG("Checking if level {} is enabled: {}", static_cast<int>(level), enabled);
    return enabled;
}

void Logger::setFlushDelay(int64_t ns) {
    LOG_DEBUG("Setting flush delay to: {} ns", ns);
    flushDelay_ = ns;
}

void Logger::setFlushLevel(Level level) {
    LOG_DEBUG("Setting flush level to: {}", static_cast<int>(level));
    flushLevel_ = level;
}

void Logger::setFlushBufferSize(uint32_t bytes) {
    LOG_DEBUG("Setting flush buffer size to: {} bytes", bytes);
    flushBufferSize_ = bytes;
}

void Logger::startPollingThread(int64_t pollInterval) {
    LOG_DEBUG("Starting polling thread with interval: {} ns", pollInterval);
    try {
        if (threadRunning_) {
            LOG_DEBUG("Thread already running");
            return;
        }

        shouldExit_ = false;
        threadRunning_ = true;

        pollingThread_ = std::thread([this]() {
            LOG_DEBUG("Polling thread started");
            while (!shouldExit_) {
                try {
                    std::unique_lock<std::mutex> lock(queueMutex_);
                    if (!messageQueue_.empty()) {
                        LOG_DEBUG("Processing {} messages", messageQueue_.size());
                        processQueuedMessages();
                    }
                    lock.unlock();
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                } catch (const std::exception& e) {
                    LOG_DEBUG("Exception in polling thread: {}", e.what());
                }
            }
            LOG_DEBUG("Polling thread stopping");
        });
        LOG_DEBUG("Polling thread created successfully");
    } catch (const std::exception& e) {
        LOG_DEBUG("Exception in startPollingThread: {}", e.what());
        threadRunning_ = false;
        throw;
    }
}

void Logger::stopPollingThread() {
    LOG_DEBUG("Stopping polling thread");
    try {
        if (!threadRunning_) {
            LOG_DEBUG("Thread not running");
            return;
        }

        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            shouldExit_ = true;
            LOG_DEBUG("Set exit flag");
        }

        if (pollingThread_.joinable()) {
            LOG_DEBUG("Joining thread");
            pollingThread_.join();
        }

        threadRunning_ = false;
        LOG_DEBUG("Polling thread stopped successfully");
    } catch (const std::exception& e) {
        LOG_DEBUG("Exception in stopPollingThread: {}", e.what());
        throw;
    }
}

void Logger::flush(bool force) {
    LOG_DEBUG("Flushing messages (force: {})", force);
    try {
        if (!outputFp_) {
            LOG_DEBUG("No output file");
            return;
        }

        std::lock_guard<std::mutex> lock(queueMutex_);
        processQueuedMessages();
        if (force) {
            LOG_DEBUG("Force flushing file");
            fflush(outputFp_);
        }
        LOG_DEBUG("Flush completed");
    } catch (const std::exception& e) {
        LOG_DEBUG("Exception in flush: {}", e.what());
        throw;
    }
}

void Logger::close() {
    LOG_DEBUG("Closing logger");
    try {
        stopPollingThread();

        std::lock_guard<std::mutex> lock(queueMutex_);
        if (outputFp_ && manageFp_) {
            LOG_DEBUG("Flushing and closing file");
            fflush(outputFp_);
            fclose(outputFp_);
        }
        outputFp_ = nullptr;
        manageFp_ = false;

        size_t remainingMessages = messageQueue_.size();
        LOG_DEBUG("Clearing {} remaining messages", remainingMessages);
        while (!messageQueue_.empty()) {
            messageQueue_.pop();
        }
        LOG_DEBUG("Logger closed successfully");
    } catch (const std::exception& e) {
        LOG_DEBUG("Exception in close: {}", e.what());
        throw;
    }
}

void Logger::processQueuedMessages() {
    if (!outputFp_) {
        LOG_DEBUG("No output file in processQueuedMessages");
        return;
    }

    static const char* levelStrings[] = {
        "调试", "信息", "警告", "错误"
    };

    size_t processedCount = 0;
    try {
        while (!messageQueue_.empty()) {
            const auto& msg = messageQueue_.front();

            auto timeT = std::chrono::system_clock::to_time_t(msg.timestamp);
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                msg.timestamp.time_since_epoch()).count() % 1000;

            std::stringstream ss;
            ss << std::put_time(std::localtime(&timeT), "%Y-%m-%d %H:%M:%S");
            ss << "." << std::setfill('0') << std::setw(3) << ms;

            std::string logLine = fmt::format("{} [{}] {}\n",
                ss.str(), levelStrings[static_cast<size_t>(msg.level)], msg.content);

            if (fwrite(logLine.data(), 1, logLine.size(), outputFp_) != logLine.size()) {
                LOG_DEBUG("Failed to write log line");
                throw std::runtime_error("Failed to write to log file");
            }
            messageQueue_.pop();
            processedCount++;
        }
        LOG_DEBUG("Processed {} messages", processedCount);
    } catch (const std::exception& e) {
        LOG_DEBUG("Exception in processQueuedMessages after processing {} messages: {}",
            processedCount, e.what());
        throw;
    }
}

}