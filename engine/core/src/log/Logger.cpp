#include "tina/log/Logger.hpp"
#include <fmt/color.h>
#include <filesystem>

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

Logger::Logger() : file_(nullptr, fclose) {}

Logger::~Logger() {
    if (running_) {
        stop();
    }
}

void Logger::setOutputFile(const std::string& filename) {
    stop();  // 确保之前的文件被关闭
    
    try {
        auto path = std::filesystem::path(filename);
        std::filesystem::create_directories(path.parent_path());
        
        FILE* fp = nullptr;
#ifdef _WIN32
        fopen_s(&fp, filename.c_str(), "a");
#else
        fp = fopen(filename.c_str(), "a");
#endif
        if (fp) {
            file_.reset(fp);
        }
    } catch (const std::exception& e) {
        fmt::print(stderr, "Failed to open log file: {}\n", e.what());
    }
}

void Logger::start() {
    if (running_) return;
    
    running_ = true;
    try {
        workerThread_ = std::thread(&Logger::processLogs, this);
    } catch (const std::exception& e) {
        running_ = false;
        fmt::print(stderr, "Failed to start logger thread: {}\n", e.what());
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
    file_.reset();  // 确保文件被关闭
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
                for (auto& msg : overflowMessages_) {
                    batchMessages.push_back(std::move(msg));
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
    if (!file_) return;  // 如果文件未打开，直接返回

    try {
        LogMessage msg;
        while (messageBuffer_.pop(msg)) {
            auto timeStr = fmt::format("{:%Y-%m-%d %H:%M:%S}", msg.timestamp);
            
            // 控制台输出
            fmt::print(fg(levelToColor(msg.level)), 
                      "[{}] [{}] [{}] {}\n",
                      timeStr,
                      levelToString(msg.level),
                      msg.module,
                      msg.message);

            // 文件输出
            if (file_) {
                fmt::print(file_.get(),
                          "[{}] [{}] [{}] {}\n",
                          timeStr,
                          levelToString(msg.level),
                          msg.module,
                          msg.message);
            }
        }

        if (file_) {
            fflush(file_.get());
        }
    } catch (const std::exception& e) {
        fmt::print(stderr, "Error during log flush: {}\n", e.what());
    }
}

} // namespace Tina
