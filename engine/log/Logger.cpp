#include "Logger.hpp"

namespace Tina {

    void Logger::setLevel(LogLevel level) {
        _logLevel = static_cast<spdlog::level::level_enum>(level);
        spdlog::set_level(_logLevel);
    }

    bool Logger::init(const std::string& logPath /* = LOG_PATH */, const size_t mode /* = STDOUT */) {
        if (_isInited) return true;
        namespace fs = std::filesystem;
        try
        {
            fs::path logFilePath(logPath);
            fs::path logFileName = logFilePath.filename();
            spdlog::filename_t basename, ext;
            std::tie(basename, ext) = spdlog::details::file_helper::split_by_extension(logFileName.string());

            spdlog::init_thread_pool(LOG_BUFFER_SIZE, std::thread::hardware_concurrency());

            std::vector<spdlog::sink_ptr> sinks;

            if (mode & STDOUT)
            {
                auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
                sinks.push_back(consoleSink);
            }

            if (mode & FILEOUT)
            {
                auto rotatingSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(logFilePath.string(), LOG_FILE_MAX_SIZE, 10);
                sinks.push_back(rotatingSink);
            }

            if (mode & ASYNC)
            {
                spdlog::set_default_logger(std::make_shared<spdlog::async_logger>(basename,sinks.begin(),sinks.end(),spdlog::thread_pool(),spdlog::async_overflow_policy::block));
            }
            else
            {
                spdlog::set_default_logger(std::make_shared<spdlog::logger>(basename, sinks.begin(), sinks.end()));
            }

            auto formatter = std::make_unique<spdlog::pattern_formatter>();
            formatter->add_flag<LogLevelFormatterFlag>('*');
            formatter->set_pattern("[%Y-%m-%d %H:%M:%S.%e] %^[%*]%$ |%t| [<%!> %s:%#]: %v");

            //formatter->set_pattern("\e[90m[%H:%M:%S.%e] [\e[0m%^%l%$\e[90m] [%*] [%s:%#]\e[0m %v");
            spdlog::set_formatter(std::move(formatter));
            
         
            //spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] %^[%*]%$ |%t| [<%!> %s:%#]: %v");
            spdlog::flush_every(std::chrono::seconds(5));
            spdlog::flush_on(spdlog::level::info);
            spdlog::set_level(_logLevel);


        }
        catch (const std::exception_ptr ex)
        {
            assert(false);
            return false;
        }
        _isInited = true;
        return true;
    }
}


