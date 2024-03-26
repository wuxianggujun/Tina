//
// Created by 33442 on 2024/3/16.
//

#ifndef GLFWWINDOW_HPP
#define GLFWWINDOW_HPP

#include <cstdio>
#include <bx/bx.h>
#include <bx/spscqueue.h>
#include <bx/thread.h>
#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#include <GLFW/glfw3.h>
#if BX_PLATFORM_LINUX
    #define GLFW_EXPOSE_NATIVE_X11
#elif BX_PLATFORM_WINDOWS
#define GLFW_EXPOSE_NATIVE_WIN32
#elif BX_PLATFORM_OSX
    #define GLFW_EXPOSE_NATIVE_COCOA
#endif
#include <GLFW/glfw3native.h>


namespace Tina
{
    class GlfwWindow
    {
    public:
        GlfwWindow(const char* title, int width, int height);

        void initialize();
        int run();
        void shutdown();

        bool isGlfwInitialized()const {
            return glfwInitialized;
        }

    private:
        static void ErrorCallback(int error, const char* description);

        [[nodiscard]] int getWidth() const
        {
            int width,height;
            glfwGetWindowSize(m_window,&width,&height);
            return width;
        }

        [[nodiscard]] int getHeight() const
        {
            int width,height;
            glfwGetWindowSize(m_window,&width,&height);
            return height;
        }

    private:
        const char* description = nullptr;
        bool glfwInitialized = true;
        GLFWwindow* m_window;
        int width;
        int height;
        const char* title;
    };
} // Tina

#endif //GLFWWINDOW_HPP
