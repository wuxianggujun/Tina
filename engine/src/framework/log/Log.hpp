#ifndef TINA_FRAMEWORK_LOG_HPP
#define TINA_FRAMEWORK_LOG_HPP

#include <spdlog/spdlog.h>
#include <spdlog/async.h>
#include <spdlog/logger.h>
#include <spdlog/fmt/fmt.h>
#include <spdlog/fmt/bundled/printf.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/msvc_sink.h>
#include <spdlog/sinks/stdout_sinks.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/common.h>
#include <spdlog/pattern_formatter.h>

namespace Tina {
    constexpr const char *LOG_PATH = "logs/default.log";
    constexpr uint32_t SINGLE_FILE_MAX_SIZE = 20 * 1024 * 1024;
    constexpr uint32_t MAX_STORAGE_DAYS = 5;

    enum LogMode {
        STDOUT = 1 << 0,
        FILE = 1 << 1,
        ASYNC = 1 << 2,
        MSVC = 1 << 3,
        DEFAULT = STDOUT | FILE | ASYNC
    };

    enum LogLevel {
        Trace = spdlog::level::trace,
        Debug = spdlog::level::debug,
        Info = spdlog::level::info,
        Warn = spdlog::level::warn,
        Error = spdlog::level::err,
        Fatal = spdlog::level::critical,
        Off = spdlog::level::off,
        N_Level = spdlog::level::n_levels
    };


    class Logger final {


    public:
        struct LogStream : public std::ostringstream {
        public:
            LogStream(const spdlog::source_loc &_loc, LogLevel _lvl, std::string_view _prefix)
                    : loc(_loc), lvl(_lvl), prefix(_prefix) {}

            ~LogStream() {
                flush();
            }

            void flush() {
                Logger::getInstance().log(loc, lvl, (prefix + str()).c_str());
            }

        private:
            spdlog::source_loc loc;
            LogLevel lvl = LogLevel::Trace;
            std::string prefix;
        };

        static Logger &getInstance() {
            static Logger logger;
            return logger;
        }

        bool init(const std::string_view logFilePath) {
            namespace fs = std::filesystem;


            return true;
        }

        void shutdown() {
            spdlog::shutdown();
        }


        template<class... Args>
        inline void log(const spdlog::source_loc &loc, LogLevel lvl, const char *fmt, const Args &... args) {
            spdlog::log(loc, static_cast<spdlog::level::level_enum>(lvl), fmt, args...);
        }

        template<class... Args>
        inline void
        printf(const spdlog::source_loc &loc, spdlog::level::level_enum lvl, const char *fmt, const Args &... args) {
            log(loc, lvl, fmt::sprintf(fmt, args...).c_str());
        }

        void setLevel(LogLevel lvl);

        void addOnOutput(const std::function<void(const std::string &message, const int &level)> &cb);

        void addOnOutput(const std::string &logger,
                         const std::function<void(const std::string &message, const int &level)> &cb);

        void flushOn(LogLevel level);

    public:
        Logger() = default;

        ~Logger() { shutdown(); }

        Logger(const Logger &) = delete;

        void operator=(const Logger &) = delete;

    private:
        std::atomic_bool _initialized = {false};
        spdlog::level::level_enum _logLevel = spdlog::level::trace;
        static thread_local std::stringstream _ss;
    };


}


#endif // TINA_FRAMEWORK_LOG_HPP
