#include "Log.hpp"
#include "LogManager.hpp"

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/sinks/basic_file_sink.h>

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/async.h>

#include <iostream>
#include <memory>
#include <chrono>
#include <thread>
#include <sstream>
#include <boost/utility.hpp>
#include <atomic>
#include <cstdlib>

namespace Tina::Manager {

	void LogManager::initialize()
	{
		auto consoleSink = std::make_unique<spdlog::sinks::stdout_color_sink_mt>();
		consoleSink->set_pattern("%^[%Y-%m-%d %H:%M:%S.%e] %v%$");

		std::vector<spdlog::sink_ptr> sinks{ consoleSink };
		auto logger = std::make_unique<spdlog::Logger>(TINA_DEFAULT_LOGGER_NAME, sinks.begin(), sinks.end());
		logger->set_level(spdlog::level::trace);
		logger->flush_on(spdlog::level::trace);
		spdlog::register_logger(logger);

	}
	void LogManager::shutdown()
	{
		spdlog::shutdown();
	}
}
