#include "Logger.hpp"
#include "Platform.hpp"
#include <csignal>
#include <cstdarg>
#include <filesystem>

#include "spdlog/pattern_formatter.h"
#include "spdlog/details/file_helper.h"
#include "spdlog/sinks/rotating_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#ifndef  TINA_PLATFORM_WINDOWS
#include <execinfo.h>
#include <unistd.h>
#include <cstdlib>
#endif

namespace Tina {
    thread_local std::stringstream Logger::_logStream;
    static uint32_t s_backtraceDepth = 0;
    std::string Logger::LOG_PATH = "log/default.log";
#ifdef TINA_PLATFORM_WINDOWS
    int vasprintf(char **str, const char *format, va_list args) {
        int size = _vscprintf(format, args);
        if (size < 0) {
            return -1;
        }
        *str = new char[size + 1];
        int result = vsnprintf_s(*str, size + 1,_TRUNCATE, format, args);
        if (result < 0) {
            delete[] *str;
            *str = nullptr;
            return -1;
        }
        return result;
    }
#endif


    void Logger::printf(const spdlog::source_loc &loc, LogLevel lvl, const char *fmt, ...) {
        auto fun = [](void *self, const char *fmt, va_list al) {
            auto thiz = static_cast<Logger *>(self);
            char *buf = nullptr;
            int len = vasprintf(&buf, fmt, al);
            if (len != -1) {
                thiz->_logStream << std::string(buf, len);
                free(buf);
            }
        };

        va_list al;
        va_start(al, fmt);

        fun(this, fmt, al);
        va_end(al);

        log(loc, lvl, _logStream.str().c_str());
        _logStream.clear();
        _logStream.str("");
    }

    void Logger::setLevel(LogLevel lvl) {
        _level = static_cast<spdlog::level::level_enum>(lvl);
        spdlog::set_level(_level);
    }

    bool Logger::init(const std::string &logPath, uint32_t mode, uint32_t threadCount, uint32_t backtrackDepth,
                      uint32_t logBufferSize) {
        if (_initialized) return true;

        try {
            std::filesystem::path logFilePath(logPath);
            std::filesystem::path logFileName = logFilePath.filename();

            auto [baseName, ext] = spdlog::details::file_helper::split_by_extension(logFileName.string());

            spdlog::init_thread_pool(logBufferSize, threadCount);
            std::vector<spdlog::sink_ptr> sinks;

            if (mode & CONSOLE) {
                auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
                sinks.push_back(consoleSink);
            }

            if (mode & FILE) {
                auto rotatingSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                    logFilePath.string(), 20 * 1024, 10);
                sinks.push_back(rotatingSink);
            }

            //异步
            if (mode & ASYNC) {
                spdlog::set_default_logger(std::make_shared<spdlog::async_logger>(baseName,
                    sinks.begin(), sinks.end(),
                    spdlog::thread_pool(),
                    spdlog::async_overflow_policy::block));
            } else {
                spdlog::set_default_logger(std::make_shared<spdlog::logger>(baseName, sinks.begin(),
                                                                            sinks.end()));
            }

            auto formatter = std::make_unique<spdlog::pattern_formatter>();
            formatter->add_flag<LevelFormatterFlag>('*').set_pattern(
                "[%Y-%m-%d %H:%M:%S.%e] %^[%*]%$ |%t| [<%!> %s:%#]: %v");

            spdlog::set_formatter(std::move(formatter));

            spdlog::flush_every(std::chrono::seconds(5));
            spdlog::flush_on(spdlog::level::info);
            spdlog::set_level(_level);

#ifdef ENABLE_LOG_BACKTRACK
		s_backtraceDepth = backtrackDepth;
		spdlog::enable_backtrace(backtrackDepth);
		auto signalHandler = [](int signal) {
#ifndef _WIN32
			std::vector<void*> array(s_backtraceDepth);
			size_t size;

			// 获取指针数组，每一个指针对应一层栈帧
			size = backtrace(array.data(), s_backtraceDepth);

			// 获取符号列表并打印到 spdlog
			char** symbols = backtrace_symbols(array.data(), size);
			if (symbols == nullptr) {
				spdlog::critical("Unable to obtain backtrace symbols.");
			} else {
				spdlog::critical("Error: signal {}", signal);
				spdlog::critical("****************** Backtrace Start ******************");
				for (size_t i = 0; i < size; ++i) {
					std::stringstream ss;
					ss << "[" << i << "]: " << symbols[i] << "\n";
					// 解析可执行文件路径和偏移地址
					char* begin_path = symbols[i];
					char* end_path = strchr(symbols[i], '(');
					char* begin_offset = strchr(symbols[i], '+');
					char* end_offset = strchr(symbols[i], ')');

					if (begin_path && end_path && begin_offset && end_offset && begin_offset < end_offset) {
						*end_path = '\0';  // 终止可执行文件路径字符串
						*end_offset = '\0';// 终止偏移地址字符串

						char syscom[256];
						sprintf(syscom, "addr2line -e %s -f -C -i %s", begin_path, begin_offset + 1);
						std::array<char, 1024> buffer;
						std::string result;
						std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(syscom, "r"), pclose);
						if (!pipe) {
							spdlog::critical("Failed to run addr2line command");
							continue;
						}
						while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
							result += buffer.data();
						}
						ss << result;
						spdlog::critical("{}", ss.str());
					} else {
						spdlog::critical("Failed to parse executable path or offset for backtrace {}", i);
					}
				}
				free(symbols);
				spdlog::critical("****************** Backtrace End ********************");
			}
#else
			spdlog::critical("Fatal error: Signal {}", signal);
			spdlog::dump_backtrace();
#endif
			std::exit(signal);
		};
		std::signal(SIGSEGV, signalHandler);
		std::signal(SIGABRT, signalHandler);
#endif
        } catch (std::exception_ptr &e) {
            return false;
        }
        _initialized = true;
        return true;
    }
}
