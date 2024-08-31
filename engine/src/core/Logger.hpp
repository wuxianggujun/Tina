#ifndef TINA_CORE_LOGGER_HPP
#define TINA_CORE_LOGGER_HPP

#include <spdlog/spdlog.h>
#include <spdlog/async.h>
#include "core/Core.hpp"

#include <spdlog/fmt/bundled/printf.h>

#include "spdlog/pattern_formatter.h"

namespace Tina {
    enum LogMode {
        CONSOLE = 1 << 0,
        FILE = 1 << 1,
        ASYNC = 1 << 2,
        ALL = CONSOLE | FILE | ASYNC
    };

    enum LogLevel : int {
        Trace = spdlog::level::trace,
        Debug = spdlog::level::debug,
        Info = spdlog::level::info,
        Warn = spdlog::level::warn,
        Error = spdlog::level::err,
        Critical = spdlog::level::critical,
        Off = spdlog::level::off,
        NotSet = spdlog::level::n_levels
    };


    class LevelFormatterFlag : public spdlog::custom_flag_formatter {
    private:
        std::pair<std::string_view, std::string_view>
        split_qualified_identifier(std::string_view qualified_identifier) {
            auto pos = qualified_identifier.find("::");
            // This should never happen, since the default logger name formatter always adds the tag
            if (pos == std::string_view::npos) {
                return {std::string_view{}, qualified_identifier};
            } else {
                return {qualified_identifier.substr(0, pos), qualified_identifier.substr(pos + 2)};
            }
        }

    public:
        void format(const spdlog::details::log_msg &msg, const std::tm &tm_time, spdlog::memory_buf_t &dest) override {
            auto [identifier,tag] = split_qualified_identifier(
                std::string_view(msg.logger_name.data(), msg.logger_name.size()));
            dest.append(identifier.data(), identifier.data() + identifier.size());
        }

        std::unique_ptr<custom_flag_formatter> clone() const override {
            return spdlog::details::make_unique<LevelFormatterFlag>();
        }
    };

    class Logger {
    public:
        struct LogStream : public std::ostringstream {
        public:
            LogStream(const spdlog::source_loc &loc, LogLevel level): _loc(loc), _level(level) {
            }

            ~LogStream() override {
                flush();
            }

            void flush() const {
                Logger::get().log(_loc, _level, str().c_str());
            }

        private:
            spdlog::source_loc _loc;
            LogLevel _level;
        };


        static Logger &get() {
            static Logger logger;
            return logger;
        }

        void shutdown() { spdlog::shutdown(); }


        template<class... Args>
        void log(const spdlog::source_loc &loc, LogLevel lvl, const char *fmt, const Args &... args);

        void printf(const spdlog::source_loc &loc, LogLevel lvl, const char *fmt, ...);

        template<class... Args>
        void fmt_printf(const spdlog::source_loc &loc, LogLevel lvl, const char *fmt, const Args &... args);

        void setLevel(LogLevel lvl);

        void setFlushOn(LogLevel lvl) const {
            spdlog::flush_on(static_cast<spdlog::level::level_enum>(lvl));
        }

        bool init(const std::string &logPath = LOG_PATH, uint32_t mode = LogMode::CONSOLE,
                  uint32_t threadCount = 1, uint32_t backtrackDepth = 128,
                  uint32_t logBufferSize = 32 * 1024);

    private:
        Logger() = default;

        ~Logger() = default;

        Logger(const Logger &) = delete;

        Logger &operator=(const Logger &) = delete;

    private:
        std::atomic_bool _initialized = false;
        spdlog::level::level_enum _level = spdlog::level::trace;
        static thread_local std::stringstream _logStream;
        static std::string LOG_PATH;
    };

    template<class... Args>
    void log(const spdlog::source_loc &loc, LogLevel lvl, const char *fmt, const Args &... args) {
#if __cplusplus >=202002L
        spdlog::log(loc, static_cast<spdlog::level::level_enum>(lvl), fmt::runtime(fmt), args...);
#else
        spdlog::log(loc,static_cast<spdlog::level::level_enum>(lvl),fmt,args...);
#endif
    }

    template<class... Args>
    void fmt_printf(const spdlog::source_loc &loc, LogLevel lvl, const char *fmt, const Args &... args) {
        log(loc, lvl, fmt::sprintf(fmt, args...).c_str());
    }
}


#endif // TINA_CORE_LOGGER_HPP
