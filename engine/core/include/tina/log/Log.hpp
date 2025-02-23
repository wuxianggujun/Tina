//
// Created by wuxianggujun on 2025/2/22.
//
#pragma once

#include "tina/core/Core.hpp"
#include <spdlog/spdlog.h>
#include <spdlog/fmt/ostr.h>
#include "spdlog/formatter.h"
#include "spdlog/details/os.h"
#include <chrono>

namespace Tina
{
    // 自定义格式化器类
    class CustomFormatter final : public spdlog::formatter
    {
    public:
        void format(const spdlog::details::log_msg& msg, spdlog::memory_buf_t& dest) override
        {
            try {
                // 获取颜色代码
                std::string color_code = get_color_code(msg.level);
                fmt::format_to(std::back_inserter(dest), FMT_STRING("{}"), color_code);

                // 获取当前时间
                auto now = std::chrono::system_clock::now();
                auto time = std::chrono::system_clock::to_time_t(now);
                
                // 使用线程安全的localtime
                std::tm tm_buf;
#ifdef _WIN32
                localtime_s(&tm_buf, &time);
#else
                localtime_r(&time, &tm_buf);
#endif

                // 获取毫秒
                const auto duration = now.time_since_epoch();
                const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count() % 1000;
                
                // 添加时间戳 [HH:MM:SS.mmm]
                fmt::format_to(std::back_inserter(dest), FMT_STRING("[{:02d}:{:02d}:{:02d}.{:03d}] "), 
                    tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec, static_cast<int>(millis));

                // 添加日志器名称和日志级别
                fmt::format_to(std::back_inserter(dest), FMT_STRING("{} [{}] "), 
                    msg.logger_name.data(), get_level_name(msg.level));

                // 添加消息
                fmt::format_to(std::back_inserter(dest), FMT_STRING("{}\033[0m\n"), 
                    std::string_view(msg.payload.data(), msg.payload.size()));
            } catch (const std::exception& e) {
                // 如果格式化失败，至少输出一些基本信息
                fmt::format_to(std::back_inserter(dest), FMT_STRING("\033[31m[ERROR] Failed to format log message: {}\033[0m\n"), e.what());
            }
        }

        std::unique_ptr<formatter> clone() const override
        {
            return std::make_unique<CustomFormatter>();
        }

    private:
        const char* get_level_name(spdlog::level::level_enum level)
        {
            switch(level)
            {
                case spdlog::level::trace: return "TRACE";
                case spdlog::level::debug: return "DEBUG";
                case spdlog::level::info: return "INFO";
                case spdlog::level::warn: return "WARN";
                case spdlog::level::err: return "ERROR";
                case spdlog::level::critical: return "FATAL";
                default: return "UNKNOWN";
            }
        }

        std::string get_color_code(spdlog::level::level_enum level)
        {
            switch(level)
            {
                case spdlog::level::trace: return "\033[37m";    // white
                case spdlog::level::debug: return "\033[36m";    // cyan
                case spdlog::level::info: return "\033[32m";     // green
                case spdlog::level::warn: return "\033[33m";     // yellow
                case spdlog::level::err: return "\033[31m";      // red
                case spdlog::level::critical: return "\033[35m";  // magenta
                default: return "\033[0m";                        // reset
            }
        }
    };

    class Log
    {
    public:
        static void init(const std::string& logFileName = "TinaEngine.log");
        static void shutdown();
        
        static const SharedPtr<spdlog::logger>& getEngineLogger();
        static const SharedPtr<spdlog::logger>& getApplicationLogger();
        
        static const std::ostringstream& getSpdOutput();
        static void clearBuffer();

    private:
        static void setupLogger(const SharedPtr<spdlog::logger>& logger, const std::vector<spdlog::sink_ptr>& sinks);
        
        static SharedPtr<spdlog::logger> m_engineLogger;
        static SharedPtr<spdlog::logger> m_applicationLogger;
        static std::ostringstream m_spdOutput;
    };
} // Tina

#define __SHORT_FILE__ (strrchr(__FILE__, '\\') ? strrchr(__FILE__, '\\') + 1 : __FILE__)

// 引擎日志宏
#define TINA_ENGINE_TRACE(...) ::Tina::Log::getEngineLogger()->trace(fmt::format(FMT_STRING("({}:{}): {}"), __SHORT_FILE__, __LINE__, fmt::format(__VA_ARGS__)))
#define TINA_ENGINE_INFO(...) ::Tina::Log::getEngineLogger()->info(fmt::format(FMT_STRING("({}:{}): {}"), __SHORT_FILE__, __LINE__, fmt::format(__VA_ARGS__)))
#define TINA_ENGINE_WARN(...) ::Tina::Log::getEngineLogger()->warn(fmt::format(FMT_STRING("({}:{}): {}"), __SHORT_FILE__, __LINE__, fmt::format(__VA_ARGS__)))
#define TINA_ENGINE_ERROR(...) ::Tina::Log::getEngineLogger()->error(fmt::format(FMT_STRING("({}:{}): {}"), __SHORT_FILE__, __LINE__, fmt::format(__VA_ARGS__)))
#define TINA_ENGINE_DEBUG(...) ::Tina::Log::getEngineLogger()->debug(fmt::format(FMT_STRING("({}:{}): {}"), __SHORT_FILE__, __LINE__, fmt::format(__VA_ARGS__)))
#define TINA_ENGINE_FATAL(...) ::Tina::Log::getEngineLogger()->critical(fmt::format(FMT_STRING("({}:{}): {}"), __SHORT_FILE__, __LINE__, fmt::format(__VA_ARGS__)))

// 应用日志宏
#define TINA_LOG_TRACE(...) ::Tina::Log::getApplicationLogger()->trace(fmt::format(FMT_STRING("({}:{}): {}"), __SHORT_FILE__, __LINE__, fmt::format(__VA_ARGS__)))
#define TINA_LOG_INFO(...) ::Tina::Log::getApplicationLogger()->info(fmt::format(FMT_STRING("({}:{}): {}"), __SHORT_FILE__, __LINE__, fmt::format(__VA_ARGS__)))
#define TINA_LOG_WARN(...) ::Tina::Log::getApplicationLogger()->warn(fmt::format(FMT_STRING("({}:{}): {}"), __SHORT_FILE__, __LINE__, fmt::format(__VA_ARGS__)))
#define TINA_LOG_ERROR(...) ::Tina::Log::getApplicationLogger()->error(fmt::format(FMT_STRING("({}:{}): {}"), __SHORT_FILE__, __LINE__, fmt::format(__VA_ARGS__)))
#define TINA_LOG_DEBUG(...) ::Tina::Log::getApplicationLogger()->debug(fmt::format(FMT_STRING("({}:{}): {}"), __SHORT_FILE__, __LINE__, fmt::format(__VA_ARGS__)))
#define TINA_LOG_FATAL(...) ::Tina::Log::getApplicationLogger()->critical(fmt::format(FMT_STRING("({}:{}): {}"), __SHORT_FILE__, __LINE__, fmt::format(__VA_ARGS__)))


// inline std::ostream& operator<<(std::ostream& os, const cd::Vec2f& vec)
// {
//     return os << std::format("({0}, {1})", vec.x(), vec.y());
// }
//
// inline std::ostream& operator<<(std::ostream& os, const cd::Vec3f& vec)
// {
//     return os << std::format("({0}, {1}, {2})", vec.x(), vec.y(), vec.z());
// }
//
// inline std::ostream& operator<<(std::ostream& os, const cd::Vec4f& vec)
// {
//     return os << std::format("({0}, {1}, {2}, {3})", vec.x(), vec.y(), vec.z(), vec.w());
// }
//
// inline std::ostream& operator<<(std::ostream& os, const cd::Quaternion& quaternion)
// {
//     return os << std::format("Vector = ({0}, {1}, {2}), Scalar = {3}", quaternion.x(), quaternion.y(), quaternion.z(), quaternion.w());
// }

