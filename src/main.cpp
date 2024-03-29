#include <iostream>
#include "core/Application.hpp"
#include "core/GlfwWindow.hpp"
#include "TestGlfwWindow.hpp"


int main(int argc, char** argv)
{
    Tina::Application<TestGlfwWindow> application;
    return application.run(argc, argv);
}
