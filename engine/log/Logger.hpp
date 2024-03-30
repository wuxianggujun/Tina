#ifndef TINA_LOGGER_HPP
#define TINA_LOGGER_HPP

#include <sstream>
#include <filesystem>
#include <boost/utility.hpp>
#include <boost/predef.h>

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


namespace Tina {

	class Logger : boost::noncopyable{

	public:
		static Logger* getInstance();

		bool init(const std::string_view& logFilePath);



	};

}


#endif // !TINA_LOGGER_HPP
