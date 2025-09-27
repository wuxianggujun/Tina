//
// 统一日志封装（基于 spdlog）
// 功能点：
// - 单例 Logger，启动时初始化格式与级别
// - 打印函数名与行号（依赖 spdlog::source_loc 与自定义宏）
// - 便捷宏：TINA_TRACE/DEBUG/INFO/WARN/ERROR/CRITICAL
//

#pragma once

#include "Core.hpp"
#include <memory>
#include <string>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace Tina::Core {

    class Log {
    public:
        enum class Level {
            Trace,
            Debug,
            Info,
            Warn,
            Error,
            Critical,
            Off
        };

        static ::spdlog::level::level_enum ToSpdLevel(Level l) {
            using L = ::spdlog::level::level_enum;
            switch (l) {
                case Level::Trace: return L::trace;
                case Level::Debug: return L::debug;
                case Level::Info: return L::info;
                case Level::Warn: return L::warn;
                case Level::Error: return L::err;
                case Level::Critical: return L::critical;
                case Level::Off: default: return L::off;
            }
        }

        // 初始化日志：可自定义 logger 名称、级别与输出 pattern
        static void Init(const char* name = "Tina",
                         Level level = Level::Info,
                         const char* pattern = "[%H:%M:%S.%e] [%^%l%$] [%s:%# %!()] %v")
        {
            auto& logger = Get();
            if (!logger) {
                logger = spdlog::stdout_color_mt(name);
            }
            logger->set_level(ToSpdLevel(level));
            logger->set_pattern(pattern);
        }

        // 获取全局 logger
        static std::shared_ptr<spdlog::logger>& Get()
        {
            static std::shared_ptr<spdlog::logger> s_logger;
            return s_logger;
        }

        static void SetLevel(Level level)
        {
            auto& logger = Get();
            if (logger) logger->set_level(ToSpdLevel(level));
        }

        static void SetPattern(const std::string& pat)
        {
            auto& logger = Get();
            if (logger) logger->set_pattern(pat);
        }
    };
}

// ========= 宏 =========
// 使用带 source_loc 的 log 调用，打印 文件/行号/函数名
// 说明：SPDLOG_FUNCTION 由 spdlog 定义，兼容 MSVC/GCC/Clang。

#define TINA_LOGGER() (::Tina::Core::Log::Get())

#define TINA_LOG_AT(level, ...)                                                   \
    do {                                                                          \
        auto& __logger = TINA_LOGGER();                                           \
        if (__logger)                                                             \
            __logger->log(::spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION}, \
                          level, __VA_ARGS__);                                    \
    } while (0)

#define TINA_TRACE(...)    TINA_LOG_AT(::spdlog::level::trace, __VA_ARGS__)
#define TINA_DEBUG(...)    TINA_LOG_AT(::spdlog::level::debug, __VA_ARGS__)
#define TINA_INFO(...)     TINA_LOG_AT(::spdlog::level::info,  __VA_ARGS__)
#define TINA_WARN(...)     TINA_LOG_AT(::spdlog::level::warn,  __VA_ARGS__)
#define TINA_ERROR(...)    TINA_LOG_AT(::spdlog::level::err,   __VA_ARGS__)
#define TINA_CRITICAL(...) TINA_LOG_AT(::spdlog::level::critical, __VA_ARGS__)
