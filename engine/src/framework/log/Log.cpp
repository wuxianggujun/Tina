
#include "Log.hpp"

namespace Tina {

    bool Logger::init(const std::string &logPath, const uint32_t mode, const uint32_t threadCount,
                      const uint32_t logBufferSize) {
        if (_initialized) return true;
        namespace fs = std::filesystem;
        try {
            fs::path logFilePath(logPath);
            fs::path logFileDir = logFilePath.parent_path();
            fs::path logFileName = logFilePath.filename();

            if (!fs::exists(logFilePath)){
                fs::create_directories(logFileDir);
            }

            spdlog::filename_t basename, ext;
            std::tie(basename,ext) = spdlog::details::file_helper::split_by_extension(logFileName.string());

            spdlog::init_thread_pool(logBufferSize, threadCount);
            std::vector<spdlog::sink_ptr> sinks;
            if (mode & STDOUT) {
                auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
                sinks.push_back(console_sink);
            }

            if (mode & MSVC){
                auto msvc_sink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
                sinks.push_back(msvc_sink);
            }

            if (mode & FILE) {
                auto rotating_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(logPath.c_str(), 1024 * 1024, 5, true);
                sinks.push_back(rotating_sink);
            }

            if (mode & ASYNC) {
                spdlog::set_default_logger(std::make_shared<spdlog::async_logger>(basename, sinks.begin(), sinks.end(),
                                                                                  spdlog::thread_pool(),
                                                                                  spdlog::async_overflow_policy::block));
            } else {
                spdlog::set_default_logger(std::make_shared<spdlog::logger>(basename, sinks.begin(), sinks.end()));
            }

            auto formatter = std::make_unique<spdlog::pattern_formatter>();
            formatter->add_flag<LogLevelFormatterFlag>('*').set_pattern(
                    "[%Y-%m-%d %H:%M:%S.%e] %^[%*]%$ |%t| [<%!> %s:%#]: %v");
            spdlog::set_formatter(std::move(formatter));
            spdlog::flush_every(std::chrono::seconds(5));
            spdlog::flush_on(spdlog::level::info);
            spdlog::set_level(_logLevel);


        } catch (std::exception_ptr e) {
            assert(false);
            return false;
        }
        _initialized = true;
        return true;
    }

    void Logger::flushOn(LogLevel level) {
        spdlog::flush_on(static_cast<spdlog::level::level_enum>(level));
    }

    void Logger::setLevel(LogLevel lvl) {
        _logLevel = static_cast<spdlog::level::level_enum>(lvl);
        spdlog::set_level(_logLevel);
    }

    void Logger::shutdown() {
        spdlog::shutdown();
    }

}
