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
#include "AbstractWindow.hpp"

namespace Tina
{
    class GlfwWindow : AbstractWindow
    {
    public:
        GlfwWindow() = default;

        void create(const char* title, int width, int height) override {
            AbstractWindow::create(title, width, height);
        }

        void initialize() override;
        int run() override;
        void shutdown() override;

        bool isGlfwInitialized()const {
            return glfwInitialized;
        }
        
	protected:
		static void onKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
		static void onWindowSizeCallback(GLFWwindow* window, int width, int height);
        static void onErrorCallback(int error, const char* description);

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

        protected:
        bool glfwInitialized = true;
        GLFWwindow* m_window;
        static bool s_showStats;
        const bgfx::ViewId clearViewId = 0;
    };
} // Tina

#endif //GLFWWINDOW_HPP
