#ifndef TINA_FRAMEWORK_LOG_HPP
#define TINA_FRAMEWORK_LOG_HPP

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
    constexpr const char* LOG_PATH = "logs/default.log";




}


#endif // TINA_FRAMEWORK_LOG_HPP
