#ifndef TINA_LOGGER_HPP
#define TINA_LOGGER_HPP

#include <iostream>
#include <string>
#include <string_view>

#include <sstream>
#include <filesystem>
#include <boost/utility.hpp>

#include <spdlog/common.h>
#include <spdlog/spdlog.h>
#include <spdlog/async.h>
#include <spdlog/logger.h>
#include <spdlog/fmt/fmt.h>
#include <spdlog/details/fmt_helper.h>
#include <spdlog/pattern_formatter.h>
#include <spdlog/fmt/bundled/printf.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/sinks/msvc_sink.h>
#include <spdlog/sinks/stdout_sinks.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>

#include "core/PlatformDetection.hpp"

namespace Tina {
    constexpr const char* LOG_PATH = "logs/default.log";
    constexpr size_t LOG_FILE_MAX_SIZE = 10 * 1024 * 1024;
    constexpr size_t LOG_MAX_STORAGE_DAYS = 5;
    constexpr size_t LOG_BUFFER_SIZE = 32 * 1024;

    enum LogMode
    {
        STDOUT = 1 << 0,
        FILEOUT = 1 << 1,
        ASYNC  = 1 << 2
    };

    enum LogLevel : size_t
    {
        Trace = 0,
        Debug = 1,
        Info = 2,
        Warn = 3,
        Error = 4,
        Fatal = 5,
        Off = 6,
        N_Levels
    };

    class Logger final : public boost::noncopyable {
    public:
        struct LogStream : public std::ostringstream
        {
        public:
            LogStream(const std::string logger, const spdlog::source_loc& loc, LogLevel level):_logger(logger),_loc(loc),_level(level) {

            }

            ~LogStream() {
                flush();
            }

            void flush() {
                Logger::getInstance().log(_loc, _level, (_logger + str()).c_str());
            }


        private:
            std::string _logger;
            spdlog::source_loc _loc;
            LogLevel _level;
        };

    public:
        class LogLevelFormatterFlag : public spdlog::custom_flag_formatter {
        public:

            std::unique_ptr<custom_flag_formatter> clone() const override
            {
                return spdlog::details::make_unique<LogLevelFormatterFlag>();
            }

            void format(const spdlog::details::log_msg& logMsg, const std::tm& tm_time, spdlog::memory_buf_t& dest) override
            {
                static spdlog::string_view_t levels[] = { "TRACE","DEBUG","INFO","WARN","ERROR","FATAL","OFF","N_LEVELS" };
                spdlog::details::fmt_helper::append_string_view(levels[logMsg.level], dest);
            }

        };

    public:
        static Logger& getInstance() {
            static Logger logger;
            return logger;
        }

        void shutdown() {
            spdlog::shutdown();
        }

        template<typename... Args>
        void log(const spdlog::source_loc& loc, LogLevel level, const char* fmt, const Args&... args);

        template<typename... Args>
        void printf(const spdlog::source_loc& loc, LogLevel level, const char* fmt, const Args&... args);

        void setLevel(LogLevel logLevel);

        void setFlushOn(LogLevel logLevel) {
            spdlog::flush_on(static_cast<spdlog::level::level_enum>(logLevel));
        }

        bool init(const std::string& logPath = LOG_PATH, const size_t mode = STDOUT);
    private:
        Logger() = default;
        ~Logger() = default;
        Logger(const Logger&) = delete;
        void operator=(const Logger&) = delete;

        void setColor();

    private:
        std::atomic_bool _isInited = { false };
        spdlog::level::level_enum _logLevel = spdlog::level::trace;
    };

    template<typename...Args>
    inline void Logger::log(const spdlog::source_loc& loc, LogLevel level, const char* fmt, const Args&... args)
    {
        spdlog::log(loc, static_cast<spdlog::level::level_enum>(level), fmt, args...);
    }


    template<typename...Args>
    void Logger::printf(const spdlog::source_loc& loc, LogLevel level, const char* fmt, const Args&... args)
    {
        spdlog::log(loc,static_cast<spdlog::level::level_enum>(level),fmt::sprintf(fmt,args...).c_str());
    }
#define LOG_TRACE(fmt,...) Tina::Logger::getInstance().printf({__FILE__, __LINE__, __FUNCTION__},Tina::LogLevel::Trace,fmt,##__VA_ARGS__);
}



#endif // !TINA_LOGGER_HPP
