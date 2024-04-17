#include "Logger.hpp"
#include <csignal>
#ifndef _WIN32
#include <cstdlib>
#include <execinfo.h>
#include <unistd.h>

#endif
thread_local std::stringstream Tina::Logger::_ss;
static uint32_t s_backtraceDepth = 0;
#ifdef _WIN32
#define xxx(sink_)                                                                                                     \
    sink_->set_color(static_cast<spdlog::level::level_enum>(LogLevel::Trace), 3);                                      \
    sink_->set_color(static_cast<spdlog::level::level_enum>(LogLevel::Debug), 1);                                      \
    sink_->set_color(static_cast<spdlog::level::level_enum>(LogLevel::Info), 2);                                       \
    sink_->set_color(static_cast<spdlog::level::level_enum>(LogLevel::Warn), 6);                                       \
    sink_->set_color(static_cast<spdlog::level::level_enum>(LogLevel::Error), 4);                                      \
    sink_->set_color(static_cast<spdlog::level::level_enum>(LogLevel::Fatal), 5);


#else
#define xxx(sink_)                                                                                                     \
    sink_->set_color(static_cast<spdlog::level::level_enum>(Trace), "\033[36m");                                       \
    sink_->set_color(static_cast<spdlog::level::level_enum>(Debug), "\033[1;34m");                                     \
    sink_->set_color(static_cast<spdlog::level::level_enum>(Info), "\033[1;32m");                                      \
    sink_->set_color(static_cast<spdlog::level::level_enum>(Warn), "\033[1;33m");                                      \
    sink_->set_color(static_cast<spdlog::level::level_enum>(Error), "\033[1;31m");                                     \
    sink_->set_color(static_cast<spdlog::level::level_enum>(Fatal), "\033[1;35m");
#endif


#ifdef _WIN32
int vasprintf(char** str, const char* format, va_list args)
{
    int size = _vscprintf(format, args);
    if (size < 0) {
        return -1;
    }

    *str = new char[size + 1];
    int result = vsnprintf_s(*str, size + 1, _TRUNCATE, format, args);
    if (result < 0) {
        delete[] * str;
        *str = nullptr;
        return -1;
    }

    return result;
}
#endif

void Tina::Logger::printf(const spdlog::source_loc& loc, LogLevel lvl, const char* fmt, ...)
{
    auto fun = [](void* self, const char* fmt, va_list al) {
        auto thiz = static_cast<Logger*>(self);
        char* buf = nullptr;
        int len = vasprintf(&buf, fmt, al);
        if (len != -1) {
            thiz->_ss << std::string(buf, len);
            free(buf);
        }
        };

    va_list al;
    va_start(al, fmt);
    fun(this, fmt, al);
    va_end(al);
    log(loc, lvl, _ss.str().c_str());
    _ss.clear();
    _ss.str("");
}

void Tina::Logger::printf_(const std::string& logger, const spdlog::source_loc& loc, LogLevel lvl, const char* fmt, ...)
{
    auto fun = [](void* self, const char* fmt, va_list al) {
        auto thiz = static_cast<Logger*>(self);
        char* buf = nullptr;
        int len = vasprintf(&buf, fmt, al);
        if (len != -1) {
            thiz->_ss << std::string(buf, len);
            free(buf);
        }
        };

    va_list al;
    va_start(al, fmt);
    fun(this, fmt, al);
    va_end(al);
    log_(logger, loc, lvl, _ss.str().c_str());
    _ss.clear();
    _ss.str("");
}

void Tina::Logger::set_level(LogLevel lvl)
{
    _logLevel = static_cast<spdlog::level::level_enum>(lvl);
    spdlog::set_level(_logLevel);
}

void Tina::Logger::set_level(const std::string& logger, LogLevel lvl)
{
    auto it = _map_exLog.find(logger);
    if (it != _map_exLog.end()) {
        it->second->set_level(static_cast<spdlog::level::level_enum>(lvl));
    }
}

void Tina::Logger::add_on_output(const std::function<void(const std::string& msg, const int& level)>& cb)
{
    auto sinks = spdlog::default_logger()->sinks();

    for (auto& sink : sinks) {
        auto sink_ptr = std::dynamic_pointer_cast<CustomRotatingFileSink>(sink);
        if (sink_ptr) {
            sink_ptr->add_on_output(cb);
        }
    }
}

void Tina::Logger::add_on_output(const std::string& logger,
    const std::function<void(const std::string&, const int& level)>& cb)
{
    auto it = _map_exLog.find(logger);
    if (it != _map_exLog.end()) {
        auto sinks = it->second->sinks();
        for (auto& sink : sinks) {
            auto sink_ptr = std::dynamic_pointer_cast<CustomRotatingFileSink>(sink);
            if (sink_ptr) {
                sink_ptr->add_on_output(cb);
            }
        }
    }
}

/***************/
bool Tina::Logger::init(const std::string& logPath, const uint32_t mode, const uint32_t threadCount,
    const uint32_t backtrackDepth, const uint32_t logBufferSize)
{
    if (_isInited)
        return true;
    try {
        std::filesystem::path log_file_path(logPath);
        std::filesystem::path log_filename = log_file_path.filename();

        spdlog::filename_t basename, ext;
        std::tie(basename, ext) = spdlog::details::file_helper::split_by_extension(log_filename.string());

        // spdlog::init_thread_pool(log_buffer_size, std::thread::hardware_concurrency());
        spdlog::init_thread_pool(logBufferSize, threadCount);
        std::vector<spdlog::sink_ptr> sinks;

        // constexpr std::size_t max_file_size = 50 * 1024 * 1024; // 50mb
        // auto rotatingSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(log_file_path, 20 * 1024, 10);

        // auto daily_sink = std::make_shared<spdlog::sinks::daily_file_sink_mt>(log_path.string(), 23, 59);
        // //日志滚动更新时间：每天23:59更新 sinks.push_back(daily_sink);

        // auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(log_path.string(), true);
        // sinks.push_back(file_sink);

        //控制台输出
        if (mode & STDOUT) {
            auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            xxx(console_sink) sinks.push_back(console_sink);
        }

        //文件输出
        if (mode & FILEOUT) {
            auto rotatingSink =
                std::make_shared<CustomRotatingFileSink>(logPath, SINGLE_FILE_MAX_SIZE, MAX_STORAGE_DAYS);
            sinks.push_back(rotatingSink);
        }

        //异步
        if (mode & ASYNC) {
            spdlog::set_default_logger(std::make_shared<spdlog::async_logger>(
                basename, sinks.begin(), sinks.end(), spdlog::thread_pool(), spdlog::async_overflow_policy::block));
        }
        else {
            spdlog::set_default_logger(std::make_shared<spdlog::logger>(basename, sinks.begin(), sinks.end()));
        }

        auto formatter = std::make_unique<spdlog::pattern_formatter>();

        formatter->add_flag<CustomLevelFormatterFlag>('*').set_pattern(
            "%^[%Y-%m-%d %H:%M:%S.%e] [%*] |%t| [<%!> %s:%#]: %v%$");

        spdlog::set_formatter(std::move(formatter));

        spdlog::flush_every(std::chrono::seconds(5));
        spdlog::flush_on(spdlog::level::info);
        spdlog::set_level(_logLevel);

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
            }
            else {
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
                        *end_path = '\0';      // 终止可执行文件路径字符串
                        *end_offset = '\0';    // 终止偏移地址字符串

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
                    }
                    else {
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
    }
    catch (std::exception_ptr e) {
        assert(false);
        return false;
    }
    _isInited = true;
    return true;
}

bool Tina::Logger::add_ExLog(const std::string& logPath, const int mode)
{
    try {
        std::filesystem::path log_file_path(logPath);
        std::filesystem::path log_filename = log_file_path.filename();

        spdlog::filename_t basename, ext;
        std::tie(basename, ext) = spdlog::details::file_helper::split_by_extension(log_filename.string());

        std::vector<spdlog::sink_ptr> sinks;

        //控制台输出
        if (mode & STDOUT) {
            auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            xxx(console_sink) sinks.push_back(console_sink);
        }
        //文件输出
        if (mode & FILEOUT) {
            auto rotatingSink =
                std::make_shared<CustomRotatingFileSink>(logPath, SINGLE_FILE_MAX_SIZE, MAX_STORAGE_DAYS);
            sinks.push_back(rotatingSink);
        }

        std::shared_ptr<spdlog::logger> exLog;
        if (mode & ASYNC) {
            exLog = std::make_shared<spdlog::async_logger>(basename, sinks.begin(), sinks.end(), spdlog::thread_pool(),
                spdlog::async_overflow_policy::block);
        }
        else {
            exLog = std::make_shared<spdlog::logger>(basename, sinks.begin(), sinks.end());
        }

        auto formatter = std::make_unique<spdlog::pattern_formatter>();

        formatter->add_flag<CustomLevelFormatterFlag>('*').set_pattern(
            "%^[%n][%Y-%m-%d %H:%M:%S.%e] [%*] |%t| [<%!> %s:%#]: %v%$");

        exLog->set_formatter(std::move(formatter));
        exLog->flush_on(spdlog::level::trace);
        exLog->set_level(spdlog::level::trace);
        _map_exLog.emplace(basename, exLog);
    }
    catch (std::exception_ptr e) {
        assert(false);
        return false;
    }
    return true;
}

Tina::CustomRotatingFileSink::CustomRotatingFileSink(spdlog::filename_t log_path, std::size_t max_size,
    std::size_t max_storage_days, bool rotate_on_open,
    const spdlog::file_event_handlers& event_handlers) :
    _log_path(log_path),
    _max_size(max_size), _max_storage_days(max_storage_days), _file_helper{ event_handlers }
{
    if (max_size == 0) {
        spdlog::throw_spdlog_ex("rotating sink constructor: max_size arg cannot be zero");
    }

    if (max_storage_days > 365 * 2) {
        spdlog::throw_spdlog_ex("rotating sink constructor: max_storage_days arg cannot exceed 2 years");
    }

    _log_parent_path = _log_path.parent_path();
    _log_filename = _log_path.filename();

    spdlog::filename_t basename, ext;
    std::tie(basename, ext) = spdlog::details::file_helper::split_by_extension(_log_filename.string());

    _log_basename = basename;

    _file_helper.open(calc_filename());
    _current_size = _file_helper.size();    // expensive. called only once

    auto now = std::chrono::system_clock::now();
    std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::localtime(&time);

    _last_rotate_day = tm.tm_mday;
    cleanup_file_();

    if (rotate_on_open && _current_size > 0) {
        rotate_();
        _current_size = 0;
    }
}

void Tina::CustomRotatingFileSink::add_on_output(
    const std::function<void(const std::string& msg, const int& level)>& cb)
{
#ifdef LOG_ADD_OUTPUT_MUTEX
    std::lock_guard<std::recursive_mutex> lock(_on_output_mx);
#endif
    _on_outputs.push_back(cb);
}

spdlog::filename_t Tina::CustomRotatingFileSink::calc_filename()
{
    auto now = std::chrono::system_clock::now();
    std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::localtime(&time);

    return _log_parent_path.empty()
        ? spdlog::fmt_lib::format("{:%Y-%m-%d}/{}_{:%Y-%m-%d_%H-%M-%S}.log", tm, _log_basename.string(), tm)
        : spdlog::fmt_lib::format("{}/{:%Y-%m-%d}/{}_{:%Y-%m-%d_%H-%M-%S}.log", _log_parent_path.string(),
            tm, _log_basename.string(), tm);
    /// logs/yyyy-mm-dd/basename_yyyy-mm-dd_h-m-s.log
}

spdlog::filename_t Tina::CustomRotatingFileSink::filename()
{
    std::lock_guard<std::mutex> lock(base_sink<std::mutex>::mutex_);
    return _file_helper.filename();
}

void Tina::CustomRotatingFileSink::sink_it_(const spdlog::details::log_msg& msg)
{
    spdlog::memory_buf_t formatted;
    base_sink<std::mutex>::formatter_->format(msg, formatted);
    auto new_size = _current_size + formatted.size();

    if (new_size > _max_size || is_daily_rotate_tp_()) {
        _file_helper.flush();
        if (_file_helper.size() > 0) {
            rotate_();
            new_size = formatted.size();
        }
    }

    {
#ifdef LOG_ADD_OUTPUT_MUTEX
        std::lock_guard<std::recursive_mutex> lock(_on_output_mx);
#endif
        for (auto& on_output : _on_outputs) {
            on_output(fmt::to_string(formatted), msg.level);
        }
    }

    _file_helper.write(formatted);
    _current_size = new_size;
}

void Tina::CustomRotatingFileSink::rotate_()
{
    auto now = std::chrono::system_clock::now();
    std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::localtime(&time);

    _last_rotate_day = tm.tm_mday;

    _file_helper.close();

    cleanup_file_();

    spdlog::filename_t filename = calc_filename();

    _file_helper.open(filename);
}

void Tina::CustomRotatingFileSink::cleanup_file_()
{
    const std::regex folder_regex(R"(\d{4}-\d{2}-\d{2})");
    // const std::chrono::hours max_age();

    for (auto& p : std::filesystem::directory_iterator(_log_parent_path)) {
        if (std::filesystem::is_directory(p)) {
            const std::string folder_name = p.path().filename().string();
            if (std::regex_match(folder_name, folder_regex)) {
                const int year = std::stoi(folder_name.substr(0, 4));
                const int mon = std::stoi(folder_name.substr(5, 7));
                const int day = std::stoi(folder_name.substr(8, 10));

                std::tm date1_tm{ 0, 0, 0, day, mon - 1, year - 1900 };
                const std::time_t date_tt = std::mktime(&date1_tm);

                const std::chrono::system_clock::time_point time = std::chrono::system_clock::from_time_t(date_tt);

                const std::chrono::system_clock::time_point now = std::chrono::system_clock::now();

                const std::chrono::duration<double> duration = now - time;

                const double days = duration.count() / (24 * 60 * 60);
                // 将时间差转换为天数

                if (days > _max_storage_days) {
                    std::filesystem::remove_all(p);
                    std::cout << "Clean up log files older than" << _max_storage_days << " days" << std::endl;
                }
            }
        }
    }
}

bool Tina::CustomRotatingFileSink::is_daily_rotate_tp_()
{
    auto now = std::chrono::system_clock::now();
    std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::localtime(&time);

    if (tm.tm_mday != _last_rotate_day)
        return true;

    return false;
}

void Tina::CustomLevelFormatterFlag::format(const spdlog::details::log_msg& _log_msg, const std::tm&,
    spdlog::memory_buf_t& dest)
{
    switch (_log_msg.level) {
#undef DEBUG
#undef ERROR
#define xx(level, msg)                                                                                                 \
    case level:                                                                                                        \
        {                                                                                                              \
            static std::string msg = #msg;                                                                             \
            dest.append(msg.data(), msg.data() + msg.size());                                                          \
            break;                                                                                                     \
        }

        // clang-format off
        xx(spdlog::level::trace, TRACE)
            xx(spdlog::level::debug, DEBUG)
            xx(spdlog::level::info, INFO)
            xx(spdlog::level::warn, WARN)
            xx(spdlog::level::err, ERROR)
            xx(spdlog::level::critical, FATAL)

#undef xx

    default: break;
    }
    // clang-format on
}

#undef xxx
