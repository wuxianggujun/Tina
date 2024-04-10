#include <iostream>
#include "core/Application.hpp"
#include "core/GlfwWindow.hpp"
#include "TestGlfwWindow.hpp"
#include "log/Logger.hpp"

int main(int argc, char** argv)
{
    Tina::Logger::getInstance().init("logs/log",Tina::STDOUT |Tina::ASYNC);
    LOG_TRACE("test {}",1);
    Tina::Application<TestGlfwWindow> application;
    return application.run(argc, argv);
}
