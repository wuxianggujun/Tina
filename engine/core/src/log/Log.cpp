//
// Created by wuxianggujun on 2025/2/22.
//

#include "tina/log/Log.hpp"

#include <iostream>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace Tina {
    SharedPtr<spdlog::logger> Log::m_engineLogger;
    SharedPtr<spdlog::logger> Log::m_applicationLogger;
    std::ostringstream Log::m_spdOutput;
    
    void Log::init(const std::string& logFileName)
    {
        try
        {
            auto consoleSink = MakeShared<spdlog::sinks::stdout_color_sink_mt>();
            consoleSink->set_formatter(std::make_unique<CustomFormatter>());

            auto fileSink = MakeShared<spdlog::sinks::basic_file_sink_mt>(logFileName, true);
            fileSink->set_formatter(std::make_unique<CustomFormatter>());

            auto spdOutputSink = MakeShared<spdlog::sinks::ostream_sink_mt>(m_spdOutput);
            spdOutputSink->set_formatter(std::make_unique<CustomFormatter>());

            std::vector<spdlog::sink_ptr> sinks = { consoleSink, fileSink, spdOutputSink };

            m_engineLogger = MakeShared<spdlog::logger>("ENGINE", sinks.begin(), sinks.end());
            m_applicationLogger = MakeShared<spdlog::logger>("TINA", sinks.begin(), sinks.end());
            
            setupLogger(m_engineLogger, sinks);
            setupLogger(m_applicationLogger, sinks);

            TINA_ENGINE_INFO("Log system initialized!");
        }
        catch (const spdlog::spdlog_ex& ex) {
            std::cerr << "Log initialization failed: " << ex.what() << std::endl;
        }
    }

    void Log::shutdown()
    {
        spdlog::shutdown();
        clearBuffer();
    }

    const SharedPtr<spdlog::logger>& Log::getEngineLogger()
    {
        return m_engineLogger;
    }

    const SharedPtr<spdlog::logger>& Log::getApplicationLogger()
    {
        return m_applicationLogger;
    }

    const std::ostringstream& Log::getSpdOutput()
    {
        return m_spdOutput;
    }

    void Log::clearBuffer()
    {
        m_spdOutput.str("");
    }

    void Log::setupLogger(const SharedPtr<spdlog::logger>& logger, const std::vector<spdlog::sink_ptr>& sinks)
    {
        spdlog::register_logger(logger);
        logger->set_level(spdlog::level::trace);
        logger->flush_on(spdlog::level::trace);
    }
} // Tina