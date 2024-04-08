#ifndef TINA_LOGGER_HPP
#define TINA_LOGGER_HPP

#include <iostream>
#include <sstream>
#include <filesystem>
#include <boost/utility.hpp>

#include <spdlog/common.h>
#include <spdlog/spdlog.h>
#include <spdlog/async.h>
#include <spdlog/logger.h>
#include <spdlog/fmt/fmt.h>
#include <spdlog/fmt/bundled/printf.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/sinks/msvc_sink.h>
#include <spdlog/sinks/stdout_sinks.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/pattern_formatter.h>

#include "core/PlatformDetection.hpp"

namespace Tina {

    enum LogMode
    {
        STDOUT = 1 << 0, //主日志控制台输出
        FILEOUT = 1<<1,  //主日志文件输出
        ASYNC = 1<<2     //异步日志模式
    };

    class LogFormatterFlag;



	class Logger final : boost::noncopyable{

    public:
        struct LogStream : public std::ostringstream
        {
        public:
            LogStream(const spdlog::source_loc&loc,spdlog::level::level_enum level,std::string_view prefix):_loc(loc),_level(level),_prefix(prefix) {

            }

            ~LogStream() {
                flush();
            }

            void flush() {
                Logger::getInstance().log(_loc,_level,(_prefix + str()).c_str());
            }

        private:
            spdlog::source_loc _loc;
            spdlog::level::level_enum _level = spdlog::level::info;
            std::string _prefix;
        };



	public:
		static Logger& getInstance();

		bool init(const std::string_view& logFilePath = "logs/log.txt",const std::string& loggerName="TinaLogger",const size_t mode = STDOUT);

        void shutdown();

        template<class... Args>
        void log(const spdlog::source_loc& loc, spdlog::level::level_enum lev, const char* fmt, const Args&... args);


        template<class... Args>
        void printf(const spdlog::source_loc& loc, spdlog::level::level_enum lev, const char* fmt, const Args&... args) {
            spdlog::log(loc, lev, fmt::sprintf(fmt, args...).c_str());
        }

        spdlog::level::level_enum level() const {
            return _log_level;
        }

        void setLevel(spdlog::level::level_enum lev) {
            _log_level = lev;
            spdlog::set_level(lev);
        }

        void setFlushOn(spdlog::level::level_enum lev) {
            spdlog::flush_on(lev);
        }

        static const char* getShortName(std::string_view path) {
            if (path.empty())
            {
                return path.data();
            }
            size_t pos = path.find_last_of("/\\");
            return path.data() + ((pos == path.npos) ? 0 : pos + 1);
        }

    private:
        Logger() = default;
        ~Logger() = default;

        Logger(const Logger&) = delete;
        Logger& operator=(const Logger&) = delete;

    private:
        std::atomic_bool isInited_{ false };
        const size_t FILE_SIZE = 1024 * 1024 * 5;
        spdlog::level::level_enum _log_level = spdlog::level::trace;

	};

    class LogFormatterFlag : public spdlog::custom_flag_formatter
    {
    public:
        void format(const spdlog::details::log_msg& _log_msg, const std::tm&,
            spdlog::memory_buf_t& dest) override {
        }

        std::unique_ptr<custom_flag_formatter> clone() const override
        {
            return spdlog::details::make_unique<LogFormatterFlag>();
        }
    };

    template<class ...Args>
    inline void Logger::log(const spdlog::source_loc& loc, spdlog::level::level_enum lev, const char* fmt, const Args & ...args)
    {
       spdlog::log(loc, lev, fmt, args...);
    }

}

#define __FILENAME__ (Tina::Logger::getShortName(__FILE__))

#define LOG_LOGGER(...) Tina::Logger::getInstance();
#define LOG_TRACE(fmt,...) Tina::Logger::getInstance().log({__FILE__, __LINE__, __FUNCTION__}, spdlog::level::trace, fmt, ##__VA_ARGS__);




#endif // !TINA_LOGGER_HPP
