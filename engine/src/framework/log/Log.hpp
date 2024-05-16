#ifndef TINA_FRAMEWORK_LOG_HPP
#define TINA_FRAMEWORK_LOG_HPP


#include <cstdint>
#include <filesystem>

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
    constexpr uint32_t LOG_BUFFER_SIZE = 32 * 1024;

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

            ~LogStream() override {
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

    public:
        class LogRotatingFileSink : public spdlog::sinks::base_sink<std::mutex> {
        public:
            LogRotatingFileSink(spdlog::filename_t logPath, std::size_t max_size, std::size_t max_storage_days,
                                bool roate_on_open = false, const spdlog::file_event_handlers &event_handles = {});

            static spdlog::filename_t calc_filename(const spdlog::filename_t &filename, std::size_t index);

            spdlog::filename_t filename();

        protected:
            void sink_it_(const spdlog::details::log_msg &msg) override;

            void flush_() override;

        private:
            void rotate_();

            bool rename_file_(const spdlog::filename_t &src_filename, const spdlog::filename_t &target_filename);

            void cleanup_old_files_();

            spdlog::filename_t base_filename_;
            std::atomic<std::size_t> max_size_;
            std::atomic<std::size_t> max_storage_days;
            std::size_t current_size_;

            std::filesystem::path _log_base_filename;
            std::filesystem::path _log_file_path;

            spdlog::details::file_helper file_helper_;

        };


    public:
        class LogLevelFormatterFlag : public spdlog::custom_flag_formatter {
        public:
            void format(const spdlog::details::log_msg &msg, const std::tm &, spdlog::memory_buf_t &dest) override {
                std::string tina_text = "Tina";
                dest.append(tina_text.data(), tina_text.data() + tina_text.size());
            }

            [[nodiscard]] std::unique_ptr<custom_flag_formatter> clone() const override {
                return spdlog::details::make_unique<LogLevelFormatterFlag>();
            }
        };

        static Logger &getInstance() {
            static Logger logger;
            return logger;
        }

        bool init(const std::string &logPath = LOG_PATH, uint32_t mode = STDOUT,
                  uint32_t threadCount = std::thread::hardware_concurrency(), uint32_t logBufferSize = LOG_BUFFER_SIZE);

        void shutdown();


        template<class... Args>
        inline void log(const spdlog::source_loc &loc, LogLevel lvl, const char *fmt, const Args &... args) {
            spdlog::log(loc, static_cast<spdlog::level::level_enum>(lvl), fmt, args...);
        }

        template<class... Args>
        inline void
        printf(const spdlog::source_loc &loc, LogLevel lvl, const char *fmt, const Args &... args) {
            log(loc, lvl, fmt::sprintf(fmt, args...).c_str());
        }

        void setLevel(LogLevel lvl);

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

#define LOG_INIT(path, mode, ...) Tina::Logger::getInstance().init(path,mode,##__VA_ARGS__);
#define LOG_TRACE(fmt, ...) Tina::Logger::getInstance().log({__FILE__, __LINE__, __FUNCTION__},Tina::LogLevel::Trace, fmt, ##__VA_ARGS__);
#define LOG_FMT_TRACE(fmt, ...) Tina::Logger::getInstance().printf({__FILE__, __LINE__, __FUNCTION__},Tina::LogLevel::Trace, fmt, ##__VA_ARGS__);


#endif // TINA_FRAMEWORK_LOG_HPP
