#include <iostream>
#include "core/Application.hpp"
#include "core/GlfwWindow.hpp"
#include "TestGlfwWindow.hpp"
#include "log/Logger.hpp"

int main(int argc, char** argv)
{
    Tina::Logger::getInstance().init("logs/log.txt",Tina::FILEOUT |Tina::ASYNC);

    LOG_TRACE("LOG_INFO {}", 1);

    Tina::Logger::getInstance().log({ __FILE__, __LINE__, __FUNCTION__ }, Tina::LogLevel::Trace,"Test {}",1);

    Tina::Application<TestGlfwWindow> application;
    return application.run(argc, argv);
}
