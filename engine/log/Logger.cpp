#include "Logger.hpp"


namespace Tina {


	Logger& Logger::getInstance()
	{
		static Logger logger;
		return logger;
	}

	bool Logger::init(const std::string_view& logFilePath,const std::string& loggerName,const size_t mode)
	{
        if (isInited_)
        {
            return true;
        }

		namespace fs = std::filesystem;

        try
        {
            fs::path logPath(logFilePath);
            fs::path logDir = logPath.parent_path();

            if (!fs::exists(logPath))
            {
                fs::create_directory(logDir);
            }

            // 异步打印日志
            constexpr std::size_t logBufferSize = 32 * 1024; // 32kb
            spdlog::init_thread_pool(logBufferSize, std::thread::hardware_concurrency());
            std::vector<spdlog::sink_ptr> sinks;

            if (mode & STDOUT)
            {
                auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
                sinks.push_back(std::move(console_sink));
            }
          
            if (mode & FILEOUT)
            {
                auto dailySink = std::make_shared<spdlog::sinks::daily_file_sink_mt>(logPath.string(), 0, 2);
                sinks.push_back(std::move(dailySink));
            }

            if (mode & ASYNC)
            {
                spdlog::set_default_logger(std::make_shared<spdlog::async_logger>(loggerName,sinks.begin(),sinks.end(),spdlog::thread_pool(),spdlog::async_overflow_policy::block));
            }
            else
            {
                spdlog::set_default_logger(std::make_shared<spdlog::logger>(loggerName, sinks.begin(), sinks.end()));
            }

            auto formatter = std::make_unique<spdlog::pattern_formatter>();

            formatter->add_flag<LogFormatterFlag>('*').set_pattern("[%Y-%m-%d %H:%M:%S.%e] %^[%*]%$ |%t| [<%!> %s:%#]: %v");

            spdlog::set_formatter(std::move(formatter));
            spdlog::flush_every(std::chrono::seconds(5)); 
            spdlog::flush_on(spdlog::level::warn);
            spdlog::set_level(_log_level);

        }
        catch (const std::exception_ptr& ex)
        {
            return false;
        }
        isInited_ = true;
		return true;
	}

    void shutdown() {
        spdlog::shutdown();
    }



}


