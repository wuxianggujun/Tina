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

#ifdef BOOST_OS_WINDOWS

#endif // DEBUG


		return false;
	}


}


