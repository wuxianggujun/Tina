#include <iostream>
#include "core/Application.hpp"
#include "core/GlfwWindow.hpp"
#include "TestGlfwWindow.hpp"
#include "log/Logger.hpp"
#include <format>

int main(int argc, char** argv)
{
    int ret = 22;
    Tina::Logger::Get().init("logs/stream.log", Tina::STDOUT | Tina::FILEOUT | Tina::ASYNC);
    Tina::Logger::Get().set_level(Tina::LogLevel::Trace);


    Tina::Logger::Get().add_ExLog("logs/EX_stream.log", Tina::STDOUT | Tina::FILEOUT | Tina::ASYNC);
    Tina::Logger::Get().set_level("EX_stream", Tina::LogLevel::Off);

    LOGTRACE() << "test" << 6666;
    logtrace("TEST:%p,%d,%x", &ret, ret + 2, &ret + 1);
    log_trace("fmt test %p", (void*)&ret);
    LOG_TRACE("test {}", 1);

    //......

    LOGFATAL() << "test" << 6666;
    logfatal("TEST:%p,%d,%x", &ret, ret + 2, &ret + 1);
    LOG_FATAL("test {}", 1);

    LOGINFO() << "test" << 6666;
    logfatal("TEST:%p,%d,%x", &ret, ret + 2, &ret + 1);
    LOG_FATAL("test {}", 1);

    Tina::Application<TestGlfwWindow> application;
    return application.run(argc, argv);
}
