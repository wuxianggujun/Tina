#ifndef TINA_LOGGER_HPP
#define TINA_LOGGER_HPP

#include <sstream>
#include <filesystem>
#include <boost/utility.hpp>

#include <spdlog/spdlog.h>
#include <spdlog/async.h>
#include <spdlog/logger.h>
#include <spdlog/fmt/fmt.h>
#include <spdlog/fmt/bundled/printf.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/sinks/msvc_sink.h>
#include <spdlog/sinks/stdout_sinks.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include "PlatformDetection.hpp"

namespace Tina {

	class Logger : boost::noncopyable{

	public:
		static Logger* getInstance();

		bool init(const std::string_view& logFilePath);

        void shutdown();

    private:
        std::atomic_bool _is_inited = false;
        spdlog::level::level_enum _log_level = spdlog::level::trace;

	};

}


#endif // !TINA_LOGGER_HPP
