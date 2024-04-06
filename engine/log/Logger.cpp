#include "Logger.hpp"


namespace Tina {

    std::atomic_bool Logger::writeToFile_{ true };
    std::atomic_bool Logger::writeToConsole_{ true };

	Logger& Logger::getInstance()
	{
		static Logger logger;
		return logger;
	}

	bool Logger::init(const std::string_view& logFilePath,const std::string& loggerName)
	{
        if (isInited_)
        {
            return true;
        }

        if (!writeToFile_ && !writeToConsole_)
        {
            std::cout << "Initialized AN EMPTY Logger!" << std::endl;
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

            spdlog::flush_every(std::chrono::seconds(3));

            // 异步打印日志
            constexpr std::size_t logBufferSize = 32 * 1024; // 32kb
            spdlog::init_thread_pool(logBufferSize, std::thread::hardware_concurrency());
            std::vector<spdlog::sink_ptr> sinks;

            if (writeToConsole_)
            {
                auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
                sinks.push_back(std::move(console_sink));
            }
          
            if (writeToFile_)
            {
                auto dailySink = std::make_shared<spdlog::sinks::daily_file_sink_mt>(logPath.string(), 0, 2);
                sinks.push_back(std::move(dailySink));
            }

            std::shared_ptr<spdlog::logger> logger = std::make_shared<spdlog::logger>(loggerName,sinks.begin(),sinks.end());
            spdlog::set_default_logger(logger);
            spdlog::set_pattern("%s(%#):[%L %D %T .%e %P %t %!] %v");
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

    void Logger::onlyToConsole()
    {
        writeToConsole_.store(false);
    }

     void Logger::onlyToFile()
    {
         writeToFile_.store(false);
    }
}


