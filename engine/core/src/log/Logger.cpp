#include "tina/log/Logger.hpp"
#include <fmt/color.h>

namespace Tina {

namespace {
    constexpr std::string_view levelToString(LogLevel level) {
        switch (level) {
            case LogLevel::Trace: return "TRACE";
            case LogLevel::Debug: return "DEBUG";
            case LogLevel::Info:  return "INFO ";
            case LogLevel::Warn:  return "WARN ";
            case LogLevel::Error: return "ERROR";
            case LogLevel::Fatal: return "FATAL";
            default:              return "UNKNOWN";
        }
    }

    fmt::color levelToColor(LogLevel level) {
        switch (level) {
            case LogLevel::Trace: return fmt::color::gray;
            case LogLevel::Debug: return fmt::color::light_blue;
            case LogLevel::Info:  return fmt::color::green;
            case LogLevel::Warn:  return fmt::color::yellow;
            case LogLevel::Error: return fmt::color::red;
            case LogLevel::Fatal: return fmt::color::purple;
            default:              return fmt::color::white;
        }
    }
}

Logger::~Logger() {
    if (running_) {
        stop();
    }
}

void Logger::setOutputFile(const String& filename) {
    stop();  // 确保之前的文件被关闭
    
    try {
        logPath_ = ghc::filesystem::path(filename.c_str());
        ghc::filesystem::create_directories(logPath_.parent_path());
        
#ifdef _WIN32
        if (fopen_s(&logFile_, filename.c_str(), "a") != 0) {
            logFile_ = nullptr;
            fmt::print(stderr, "Failed to open log file: {}\n", filename.c_str());
        }
#else
        logFile_ = fopen(filename.c_str(), "a");
        if (!logFile_) {
            fmt::print(stderr, "Failed to open log file: {}\n", filename.c_str());
        }
#endif
    } catch (const std::exception& e) {
        fmt::print(stderr, "Failed to create log directory: {}\n", e.what());
    }
}

void Logger::start() {
    if (running_) return;

    // 检查系统资源
    if (!logFile_) {
        fmt::print(stderr, "Cannot start logger: No output file set\n");
        return;
    }

    running_ = true;
    workerThread_ = std::thread(&Logger::processLogs, this);

    // 验证线程是否成功启动
    if (!workerThread_.joinable()) {
        running_ = false;
        fmt::print(stderr, "Failed to start logger thread: Thread not joinable\n");
        return;
    }
}

void Logger::stop() {
    if (!running_) return;
    
    running_ = false;
    flushCV_.notify_all();
    
    if (workerThread_.joinable()) {
        workerThread_.join();
    }
    
    flush();
    if (logFile_) {
        fclose(logFile_);
        logFile_ = nullptr;
    }
}

void Logger::processLogs() {
    std::vector<LogMessage> batchMessages;
    batchMessages.reserve(64);

    while (running_) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            flushCV_.wait_for(lock, std::chrono::milliseconds(100));
        }

        LogMessage msg;
        while (messageBuffer_.pop(msg)) {
            batchMessages.push_back(std::move(msg));
            
            if (batchMessages.size() >= 64) {
                flush();
                batchMessages.clear();
            }
        }

        if (!batchMessages.empty()) {
            flush();
            batchMessages.clear();
        }

        // 处理溢出消息
        {
            std::lock_guard<std::mutex> lock(overflowMutex_);
            if (!overflowMessages_.empty()) {
                for (auto& log_message : overflowMessages_) {
                    batchMessages.push_back(std::move(log_message));
                }
                overflowMessages_.clear();
                if (!batchMessages.empty()) {
                    flush();
                    batchMessages.clear();
                }
            }
        }
    }
}

void Logger::flush() {
    try {
        LogMessage msg;
        while (messageBuffer_.pop(msg)) {
            auto timeStr = fmt::format("{:%Y-%m-%d %H:%M:%S}", msg.timestamp);
            
            // 控制台输出
            fmt::print(fg(levelToColor(msg.level)), 
                      "[{}] [{}] [{}] {}\n",
                      timeStr,
                      levelToString(msg.level),
                      msg.module.c_str(),
                      msg.message.c_str());

            // 文件输出
            if (logFile_) {
                fmt::print(logFile_,
                          "[{}] [{}] [{}] {}\n",
                          timeStr,
                          levelToString(msg.level),
                          msg.module.c_str(),
                          msg.message.c_str());
                fflush(logFile_);
            }
        }
    } catch (const std::exception& e) {
        fmt::print(stderr, "Error during log flush: {}\n", e.what());
    }
}

} // namespace Tina
