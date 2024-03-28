#include <iostream>
#include "Application.hpp"
#include "GlfwWindow.hpp"

int main(int argc, char** argv)
{
    Tina::Application<Tina::GlfwWindow> application;
    return application.run(argc, argv);
}
