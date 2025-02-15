//
// Created by wuxianggujun on 2025/2/5.
//

#include <utility>
#include <bx/handlealloc.h>
#include <bx/allocator.h>
#include "tina/window/Window.hpp"
#include "tina/log/Logger.hpp"
#include "tina/window/GlfwMemoryManager.hpp"

namespace Tina {
    Window::Window(WindowManager* windowManager, WindowHandle handle, const WindowConfig& config)
        : m_windowManager(windowManager), m_windowHandle(handle), m_handle(nullptr)
    {
        size_t beforeMem = GlfwMemoryManager::getCurrentAllocated();

        m_handle = glfwCreateWindow(config.width, config.height, config.title, nullptr, nullptr);
        if (!m_handle)
        {
            TINA_LOG_ERROR("Window::Window", "Failed to create GLFW window");
            return;
        }
        glfwSetWindowUserPointer(m_handle, this);
        size_t afterMem = GlfwMemoryManager::getCurrentAllocated();
        TINA_LOG_DEBUG("Window creation memory usage: {} bytes", afterMem - beforeMem);
    }

    Window::~Window()
    {
        if (m_handle)
        {
            TINA_LOG_DEBUG("Destroying window");
            glfwSetWindowUserPointer(m_handle, nullptr);  // 清除用户指针
            glfwDestroyWindow(m_handle);
            m_handle = nullptr;
        }
    }

    bool Window::shouldClose() const
    {
        return glfwWindowShouldClose(m_handle);
    }

    void Window::setTitle(const char* title)
    {
        glfwSetWindowTitle(m_handle, title);
    }

    void Window::setPosition(int32_t x, int32_t y)
    {
        glfwSetWindowPos(m_handle, x, y);
    }

    void Window::setFullscreen(bool fullscreen)
    {
        if (fullscreen)
        {
            // 获取主显示器
            GLFWmonitor* monitor = glfwGetPrimaryMonitor();
            const GLFWvidmode* mode = glfwGetVideoMode(monitor);
            glfwSetWindowMonitor(m_handle, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
        }
        else
        {
            int x, y, width, height;
            glfwGetWindowPos(m_handle, &x, &y);
            glfwGetWindowSize(m_handle, &width, &height);
            glfwSetWindowMonitor(m_handle, nullptr, x, y, width, height, 0);
        }
    }

} // Tina