//
// Created by 33442 on 2024/3/14.
//

#ifndef LOG_HPP
#define LOG_HPP

#include <spdlog/spdlog.h>
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

namespace Tina
{
    class Logger final : boost::noncopyable
    {
    public:
        static Logger& Instance();
        bool init(const std::string& filePath = "logs/log.txt", const std::string& loggerName = "Logger",
                  spdlog::level::level_enum level = spdlog::level::info);
        void setLevel(spdlog::level::level_enum level = spdlog::level::info);

        static void onlyToConsole()
        {
            writeToFile_ = false;
        }

        static void onlyToFile()
        {
            writeToConsole_ = false;
        }

        static void shutdown()
        {
            spdlog::shutdown();
        }

    private:
        Logger() = default;
        ~Logger() = default;
        Logger(const Logger&) = delete;
        Logger& operator=(const Logger&) = delete;

    private:
        std::atomic<bool> isInited_{false};
        static std::atomic<bool> writeToConsole_;
        static std::atomic<bool> writeToFile_;
    };
}

// spd console
#define TRACE(...) SPDLOG_LOGGER_TRACE(spdlog::default_logger_raw(), __VA_ARGS__);
#define DEBUG(...) SPDLOG_LOGGER_DEBUG(spdlog::default_logger_raw(),__VA_ARGS__);
#define INFO(...) SPDLOG_LOGGER_INFO(spdlog::default_logger_raw(),__VA_ARGS__);
#define WARN(...) SPDLOG_LOGGER_WARN(spdlog::default_logger_raw(),__VA_ARGS__);
#define ERROR(...) SPDLOG_LOGGER_ERROR(spdlog::default_logger_raw(),__VA_ARGS__);
#define CRITICAL(...) SPDLOG_LOGGER_CRITICAL(spdlog::default_logger_raw(),__VA_ARGS__); abort();

#define LOG_LEVEL_INFO spdlog::set_level(spdlog::level::info);
#define LOG_LEVEL_DEBUG spdlog::set_level(spdlog::level::debug);
#define LOG_LEVEL_TRACE spdlog::set_level(spdlog::level::trace);
#define LOG_LEVEL_WARN spdlog::set_level(spdlog::level::warn);
#define LOG_LEVEL_ERROR spdlog::set_level(spdlog::level::err);
#define LOG_LEVEL_CRITICAL spdlog::set_level(spdlog::level::critical);

// todo need to improve
// #define LOGGER Logger::Instance().init("Logger","logs/log.txt",spdlog::level::trace);

#define LOGGER(...) Tina::Logger::Instance().init(__VA_ARGS__);
#define LOGGER_WITH_NAME(name) Tina::Logger::Instance().init("logs/log.txt",name);
#define ONLY_TO_CONSOLE Tina::Logger::onlyToConsole();
#define ONLY_TO_FILE Tina::Logger::onlyToFile();

#endif //LOG_HPP
