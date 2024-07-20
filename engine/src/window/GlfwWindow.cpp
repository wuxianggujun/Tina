//
// Created by wuxianggujun on 2024/7/12.
//

#include "GLFWWindow.hpp"

#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

namespace Tina
{
    GlfwWindow::GlfwWindow() : m_window(nullptr, GlfwWindowDeleter())
    {
    }


    void GlfwWindow::create(WindowConfig config)
    {
        

    }

    void GlfwWindow::render()
    {
    }

    void GlfwWindow::destroy()
    {
    }

    void GlfwWindow::pollEvents()
    {
    }

    bool GlfwWindow::shouldClose()
    {
        return true;
    }
} // Tina
