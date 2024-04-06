#ifndef TINA_LOGGER_HPP
#define TINA_LOGGER_HPP

#include <iostream>
#include <sstream>
#include <filesystem>
#include <boost/utility.hpp>

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

#include "core/PlatformDetection.hpp"

#define __FILENAME__ (Tina::Logger::getShortName(__FILE__))


#define TINA_LOGGER(...) Tina::Logger::getInstance();


namespace Tina {

	class Logger : boost::noncopyable{

	public:
		static Logger& getInstance();

		bool init(const std::string_view& logFilePath = "logs/log.txt",const std::string& loggerName="TinaLogger");

        void shutdown();

        template<class... Args>
        void log(const spdlog::source_loc& loc, spdlog::level::level_enum lev, const char* fmt, const Args&... args) {
            spdlog::log(loc, lev, fmt, args...);
        }

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

        static void onlyToConsole();

        static void onlyToFile();

    private:
        Logger() = default;
        ~Logger() = default;

        Logger(const Logger&) = delete;
        Logger& operator=(const Logger&) = delete;

    private:
        std::atomic_bool isInited_{ false };
        static std::atomic_bool writeToConsole_;
        static std::atomic_bool writeToFile_;
        const size_t FILE_SIZE = 1024 * 1024 * 5;
        spdlog::level::level_enum _log_level = spdlog::level::trace;

	};

}


#endif // !TINA_LOGGER_HPP
