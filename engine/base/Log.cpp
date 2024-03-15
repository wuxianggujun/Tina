//
// Created by 33442 on 2024/3/14.
//

#include "Log.hpp"
#include <spdlog/pattern_formatter.h>

#include "Util.hpp"

namespace Tina
{
    std::atomic<bool> Logger::writeToConsole_{true};
    std::atomic<bool> Logger::writeToFile_{true};

    class co_formatter_flag : public spdlog::custom_flag_formatter
    {
    public:
        void format(const spdlog::details::log_msg& msg, const std::tm& tm_time, spdlog::memory_buf_t& dest) override
        {
            std::string coID = std::to_string(Util::gettid());
            dest.append(coID.data(), coID.data() + coID.size());
        }

        std::unique_ptr<custom_flag_formatter> clone() const override {
            return spdlog::details::make_unique<co_formatter_flag>();
        }
    };

    Logger& Logger::Instance() {
        static Logger log;
        return log;
    }

    bool Logger::init(const std::string& filePath /* = "logs/log.txt" */, const std::string& loggerName /* = "Logger" */, spdlog::level::level_enum level /* = spdlog::level::info */) {
        if (isInited_) {
            return true;
           
        }
        if (!writeToFile_ && !writeToConsole_)
        {
            std::cout << "Initialized AN EMPTY Logger!" << std::endl;
            return true;
        }

        try
        {
            spdlog::flush_every(std::chrono::seconds(3));

            std::vector<spdlog::sink_ptr> sinks;

            if (writeToConsole_)
            {
                auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
                sinks.push_back(std::move(console_sink));
            }

            if (writeToFile_)
            {
                auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(filePath, 1024 * 1024 * 5, 5, false);
                sinks.push_back(std::move(file_sink));
            }

            std::shared_ptr<spdlog::logger> logger = std::make_shared<spdlog::logger>(loggerName, sinks.begin(), sinks.end());
            logger->set_level(level);

			auto formatter = Util::make_unique<spdlog::pattern_formatter>();
			formatter->add_flag<co_formatter_flag>('*').set_pattern("%Y-%m-%d %H:%M:%S [%l] [tid : %t] [%s : %# <%!>] [coId : %*] %v");
			// logger->set_pattern("%Y-%m-%d %H:%M:%S [%l] [tid : %t] [%s : %# <%!>] %v");
			// spdlog::set_formatter();
			logger->set_formatter(std::move(formatter));

			logger->flush_on(spdlog::level::warn);
			spdlog::set_default_logger(logger);

        }
        catch (const spdlog::spdlog_ex& ex)
        {
            std::cout << "Log initialization failed: " << ex.what() << std::endl;
        }

        isInited_ = true;
        return true;
    }

    void Logger::setLevel(spdlog::level::level_enum level) {
        spdlog::set_level(level);
    }

} // TINA
