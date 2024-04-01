#include "Logger.hpp"


namespace Tina {

	Logger* Logger::getInstance()
	{
		static Logger logger;
		return logger;
	}

	bool Logger::init(const std::string_view& logFilePath)
	{
		namespace fs = std::filesystem;

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

		auto dailySink = std::make_shared<spdlog::sinks::daily_file_sink_mt>(logPath.string(), 0, 2);
		sinks.push_back(dailySink);

#if defined(TINA_PLATFORM_WINDOWS) && defined(_DEBUG) && !defined(NO_CONSOLE_LOG)
        auto ms_sink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
        sinks.push_back(ms_sink);
#endif // DEBUG

#if !defined(TINA_PLATFORM_WINDOWS) && !defined(NO_CONSOLE_LOG)
        auto consolt_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        sinks.push_back(consolt_sink);
#endif
        spdlog::set_default_logger(std::make_shared<spdlog::logger>("", sinks.begin(), sinks.end()));
        spdlog::set_pattern("%s(%#):[%L %D %T .%e %P %t %!] %v");
        spdlog::flush_on(spdlog::level::warn);
        spdlog::set_level(_log_level);
		return true;
	}

    void shutdown() {
        spdlog::shutdown();
    }

}


