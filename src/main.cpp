#include <iostream>
#include "core/Application.hpp"
#include "core/GlfwWindow.hpp"
#include "TestGlfwWindow.hpp"
#include "log/Logger.hpp"

int main(int argc, char** argv)
{
    Tina::Logger::getInstance().init();
    Tina::Logger::getInstance().onlyToFile();

    Tina::Application<TestGlfwWindow> application;
    return application.run(argc, argv);
}
