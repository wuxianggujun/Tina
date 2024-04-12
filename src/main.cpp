#include <iostream>
#include "core/Application.hpp"
#include "core/GlfwWindow.hpp"
#include "TestGlfwWindow.hpp"
#include "log/Logger.hpp"
#include <format>

int main(int argc, char** argv)
{

    LOG_LOGGER_INIT("logs/stream.log", Tina::STDOUT | Tina::FILEOUT | Tina::ASYNC);
  

    Tina::Application<TestGlfwWindow> application;
    return application.run(argc, argv);
}
