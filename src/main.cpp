#include <iostream>
#include "engine/core/Application.hpp"
#include "engine/core/GlfwWindow.hpp"
#include "TestGlfwWindow.hpp"


int main(int argc, char** argv)
{
    Tina::Application<TestGlfwWindow> application;
    return application.run(argc, argv);
}
