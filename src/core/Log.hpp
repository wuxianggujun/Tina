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
#include <vector>
#include <filesystem>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/basic_file_sink.h>

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
                         const char* pattern = "[%H:%M:%S.%e] [%^%l%$] [%s:%# %!()] %v",
                         const char* file_path = nullptr,
                         size_t max_file_size = 10ull * 1024ull * 1024ull, // 10 MB
                         size_t max_files = 3,
                         bool truncate = false)
        {
            auto& logger = Get();
            if (!logger) {
                std::vector<spdlog::sink_ptr> sinks;
                auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
                sinks.push_back(console_sink);

                if (file_path && *file_path) {
                    try {
                        namespace fs = std::filesystem;
                        fs::path p(file_path);
                        if (p.has_parent_path()) {
                            fs::create_directories(p.parent_path());
                        }
                        spdlog::sink_ptr file_sink;
                        if (max_file_size > 0 && max_files > 0 && !truncate) {
                            file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(p.string(), max_file_size, max_files);
                        } else {
                            file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(p.string(), truncate);
                        }
                        sinks.push_back(file_sink);
                    } catch (const std::exception& e) {
                        // 仅控制台提示，避免抛出影响程序启动
                        auto fallback = spdlog::stdout_color_mt(name);
                        fallback->set_level(ToSpdLevel(level));
                        fallback->set_pattern(pattern);
                        fallback->warn("日志文件创建失败: {}", e.what());
                        logger = fallback;
                    }
                }

                if (!logger) {
                    if (!sinks.empty()) {
                        auto lg = std::make_shared<spdlog::logger>(name, sinks.begin(), sinks.end());
                        spdlog::register_logger(lg);
                        logger = lg;
                    } else {
                        logger = spdlog::stdout_color_mt(name);
                    }
                }
            }
            logger->set_level(ToSpdLevel(level));
            // 允许调用方传入 pattern=nullptr/空串以采用内置默认格式
            const char* kDefaultPattern = "[%H:%M:%S.%e] [%^%l%$] [%s:%# %!()] %v";
            const char* pat = (pattern && *pattern) ? pattern : kDefaultPattern;
            logger->set_pattern(pat);
            logger->flush_on(ToSpdLevel(Level::Info));
        }

        // 便捷重载：仅设置级别（使用默认名称与默认格式，控制台输出）
        static void Init(Level level)
        {
            Init("Tina", level);
        }

        // 便捷重载：设置级别与文件输出（使用默认格式）
        static void InitWithFile(const char* name,
                                 Level level,
                                 const char* file_path,
                                 size_t max_file_size = 10ull * 1024ull * 1024ull,
                                 size_t max_files = 3,
                                 bool truncate = false)
        {
            Init(name, level, /*pattern*/nullptr, file_path, max_file_size, max_files, truncate);
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

        static void Shutdown()
        {
            auto& logger = Get();
            if (logger) {
                logger->flush();
                auto name = logger->name();
                spdlog::drop(name);
                logger.reset();
            }
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
