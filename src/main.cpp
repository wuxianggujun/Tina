#include <iostream>
#include "core/Application.hpp"
#include "core/GlfwWindow.hpp"
#include "TestGlfwWindow.hpp"
#include "log/Logger.hpp"

int main(int argc, char** argv)
{
    Tina::Logger::getInstance().init("logs/stream.log","Logger", Tina::STDOUT);
    LOG_TRACE("test");
    Tina::Logger::getInstance().log({ __FILE__, __LINE__, __FUNCTION__ }, spdlog::level::trace,"test {}",nullptr);
    Tina::Application<TestGlfwWindow> application;
    return application.run(argc, argv);
}
