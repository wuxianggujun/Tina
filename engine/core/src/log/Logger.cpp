#include "tina/log/Logger.hpp"

#include <array>
#include <atomic>

#include <vector>
#include <memory>
#include <cstdio>
#include <corecrt_io.h>
#include <fcntl.h>
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
        fmt::print("Setting up log file: {}\n", filename.c_str());
        
        // 确保日志目录存在
        logPath_ = ghc::filesystem::path(filename.c_str());
        if (!logPath_.parent_path().empty()) {
            ghc::filesystem::create_directories(logPath_.parent_path());
            fmt::print("Created log directory: {}\n", logPath_.parent_path().string());
        }
        
        // 关闭现有文件
        if (logFile_) {
            fmt::print("Closing existing log file\n");
            fclose(logFile_);
            logFile_ = nullptr;
        }
        
        fmt::print("Opening log file in binary mode: {}\n", filename.c_str());
        
        // 检查文件权限
        if (ghc::filesystem::exists(logPath_)) {
            ghc::filesystem::file_status status = ghc::filesystem::status(logPath_);
            if ((status.permissions() & ghc::filesystem::perms::owner_write) == ghc::filesystem::perms::none) {
                fmt::print(stderr, "File is read-only\n");
                return;
            }
        }
        
#ifdef TINA_PLATFORM_WINDOWS
        // Windows平台特定代码
        HANDLE fileHandle = CreateFileA(
            filename.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ,
            nullptr,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );
        
        if (fileHandle == INVALID_HANDLE_VALUE) {
            DWORD error = GetLastError();
            char errMsg[256];
            FormatMessageA(
                FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                nullptr,
                error,
                MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                errMsg,
                sizeof(errMsg),
                nullptr
            );
            fmt::print(stderr, "Failed to create file: {} (error: {})\n", errMsg, error);
            return;
        }
        
        // 将 HANDLE 转换为 FILE*
        int fd = _open_osfhandle((intptr_t)fileHandle, _O_BINARY | _O_RDWR);
        if (fd == -1) {
            CloseHandle(fileHandle);
            fmt::print(stderr, "Failed to convert handle to file descriptor\n");
            return;
        }
        
        logFile_ = _fdopen(fd, "wb");
        if (!logFile_) {
            _close(fd);
            fmt::print(stderr, "Failed to create FILE* from file descriptor\n");
            return;
        }
#else
        // 非Windows平台代码
        logFile_ = fopen(filename.c_str(), "wb");
        if (!logFile_) {
            fmt::print(stderr, "Failed to open log file '{}': {}\n", filename.c_str(), strerror(errno));
            return;
        }
#endif
        
        fmt::print("Log file opened successfully, writing BOM\n");
        
        // 写入UTF-8 BOM
        const unsigned char utf8_bom[3] = { 0xEF, 0xBB, 0xBF };
        size_t bomWritten = fwrite(utf8_bom, 1, sizeof(utf8_bom), logFile_);
        if (bomWritten != sizeof(utf8_bom)) {
            fmt::print(stderr, "Failed to write UTF-8 BOM: wrote {} of {} bytes\n", 
                      bomWritten, sizeof(utf8_bom));
            fclose(logFile_);
            logFile_ = nullptr;
            return;
        }
        
        // 设置文件缓冲区为无缓冲模式
        if (setvbuf(logFile_, nullptr, _IONBF, 0) != 0) {
            fmt::print(stderr, "Failed to set file buffer mode: {}\n", strerror(errno));
        }
        
        // 验证文件是否可写
        auto initMsg = fmt::format("=== Log Started at {} ===\n", 
                                 fmt::format("{:%Y-%m-%d %H:%M:%S}", 
                                           std::chrono::system_clock::now()));
        
        size_t initMsgLen = initMsg.length();
        size_t written = fwrite(initMsg.c_str(), 1, initMsgLen, logFile_);
        
        if (written != initMsgLen) {
            fmt::print(stderr, "Failed to write initial log message: wrote {} of {} bytes\n",
                      written, initMsgLen);
            fclose(logFile_);
            logFile_ = nullptr;
            return;
        }
        
        // 确保数据写入磁盘
        if (fflush(logFile_) != 0) {
            fmt::print(stderr, "Failed to flush initial message: {}\n", strerror(errno));
        }
        
#ifdef TINA_PLATFORM_WINDOWS
        if (_commit(_fileno(logFile_)) != 0) {
            fmt::print(stderr, "Failed to commit initial message: {}\n", strerror(errno));
        }
#else
        if (fsync(fileno(logFile_)) != 0) {
            fmt::print(stderr, "Failed to sync initial message: {}\n", strerror(errno));
        }
#endif
        
        fmt::print("Successfully initialized log file\n");
    } catch (const std::exception& e) {
        fmt::print(stderr, "Failed to setup log file '{}': {}\n", filename.c_str(), e.what());
        if (logFile_) {
            fclose(logFile_);
            logFile_ = nullptr;
        }
    }
}

void Logger::start() {
    if (running_) {
        fmt::print("Logger is already running\n");
        return;
    }

    if (!logFile_) {
        fmt::print(stderr, "Cannot start logger: No output file set\n");
        return;
    }

    running_ = true;
    workerThread_ = std::thread(&Logger::processLogs, this);

    if (!workerThread_.joinable()) {
        running_ = false;
        fmt::print(stderr, "Failed to start logger thread\n");
        return;
    }
    
    fmt::print("Logger started successfully\n");
}

void Logger::stop() {
    if (!running_) return;
    
    fmt::print("Stopping logger...\n");
    running_ = false;
    flushCV_.notify_all();
    
    if (workerThread_.joinable()) {
        workerThread_.join();
    }
    
    if (logFile_) {
        auto endMsg = fmt::format("=== Log Ended at {} ===\n",
                                fmt::format("{:%Y-%m-%d %H:%M:%S}",
                                          std::chrono::system_clock::now()));
        
        size_t endMsgLen = endMsg.length();
        size_t written = fwrite(endMsg.c_str(), 1, endMsgLen, logFile_);
        
        if (written != endMsgLen) {
            fmt::print(stderr, "Failed to write end message: wrote {} of {} bytes\n",
                      written, endMsgLen);
        }
        
        if (fflush(logFile_) != 0) {
            fmt::print(stderr, "Failed to flush final message: {}\n", strerror(errno));
        }
        
        fclose(logFile_);
        logFile_ = nullptr;
    }
    
    fmt::print("Logger stopped\n");
}

void Logger::processLogs() {
    fmt::print("Log processing thread started\n");
    
    while (running_) {
        bool hasMessages = false;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            flushCV_.wait_for(lock, std::chrono::milliseconds(100));
        }

        LogMessage msg;
        while (messageBuffer_.pop(msg)) {
            hasMessages = true;
            // 将系统时间转换为本地时间
            auto now = std::chrono::system_clock::to_time_t(msg.timestamp);
            std::tm local_tm{};
            localtime_s(&local_tm, &now);
            auto timeStr = fmt::format("{:%Y-%m-%d %H:%M:%S}", local_tm);
            auto logStr = fmt::format("[{}] [{}] [{}] {}\n",
                                    timeStr,
                                    levelToString(msg.level),
                                    msg.module.c_str(),
                                    msg.message.c_str());
            
            // 控制台输出
            fmt::print(fg(levelToColor(msg.level)), "{}", logStr);
            
            // 文件输出
            if (logFile_) {
                size_t len = logStr.length();
                size_t written = fwrite(logStr.c_str(), 1, len, logFile_);
                
                if (written != len) {
                    fmt::print(stderr, "Failed to write log message: wrote {} of {} bytes (error: {})\n",
                             written, len, strerror(errno));
                    continue;
                }
                
                if (fflush(logFile_) != 0) {
                    fmt::print(stderr, "Failed to flush log message: {}\n", strerror(errno));
                }
                
#ifdef TINA_PLATFORM_WINDOWS
                if (_commit(_fileno(logFile_)) != 0) {
                    fmt::print(stderr, "Failed to commit log message: {}\n", strerror(errno));
                }
#else
                if (fsync(fileno(logFile_)) != 0) {
                    fmt::print(stderr, "Failed to sync log message: {}\n", strerror(errno));
                }
#endif
            }
        }

        if (hasMessages) {
            fmt::print("Processed batch of messages\n");
        }
    }
    
    fmt::print("Log processing thread stopped\n");
}

void Logger::log(LogLevel level, std::string_view module, std::string_view message) {
    if (level < minLevel_) return;

    try {
        LogMessage msg{
            std::chrono::system_clock::now(),
            level,
            String(module),
            String(message)
        };

        if (!messageBuffer_.push(std::move(msg))) {
            // Buffer full, handle overflow
            std::lock_guard<std::mutex> lock(overflowMutex_);
            overflowMessages_.push_back(std::move(msg));
            fmt::print(stderr, "Log buffer full, message moved to overflow\n");
        }
        flushCV_.notify_one();
    } catch (const std::exception& e) {
        fmt::print(stderr, "Logging failed: {}\n", e.what());
    }
}

void Logger::flush() {
    if (!running_) return;
    
    try {
        LogMessage msg;
        while (messageBuffer_.pop(msg)) {
            auto timeStr = fmt::format("{:%Y-%m-%d %H:%M:%S}", msg.timestamp);
            auto logStr = fmt::format("[{}] [{}] [{}] {}\n",
                                    timeStr,
                                    levelToString(msg.level),
                                    msg.module.c_str(),
                                    msg.message.c_str());
            
            // 控制台输出
            fmt::print(fg(levelToColor(msg.level)), "{}", logStr);
            
            // 文件输出
            if (logFile_) {
                if (fputs(logStr.c_str(), logFile_) == EOF) {
                    fmt::print(stderr, "Failed to write to log file\n");
                    continue;
                }
                
                // 立即刷新文件缓冲区
                if (fflush(logFile_) != 0) {
                    fmt::print(stderr, "Failed to flush log file\n");
                }
            }
        }
    } catch (const std::exception& e) {
        fmt::print(stderr, "Error during log flush: {}\n", e.what());
        // 尝试重新打开文件
        if (logFile_) {
            fclose(logFile_);
            logFile_ = nullptr;
            if (!logPath_.empty()) {
                setOutputFile(String(logPath_.string()));
            }
        }
    }
}

} // namespace Tina
