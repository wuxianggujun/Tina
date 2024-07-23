//
// Created by wuxianggujun on 2024/7/12.
//

#ifndef TINA_WINDOW_GLFWWINDOW_HPP
#define TINA_WINDOW_GLFWWINDOW_HPP

#include "Window.hpp"
#include <bx/bx.h>
#include <bimg/bimg.h>
#include <bgfx/bgfx.h>

#include <bgfx/platform.h>

#include <GLFW/glfw3.h>

#if BX_PLATFORM_LINUX
#if TINA_CONFIG_USE_WAYLAND
    #define GLFW_EXPOSE_NATIVE_WAYLAND
#else
    #define GLFW_EXPOSE_NATIVE_X11
    #define define GLFW_EXPOSE_NATIVE_GLX
#endif

#elif BX_PLATFORM_OSX
    #define GLFW_EXPOSE_NATIVE_COCOA
    #define GLFW_EXPOSE_NATIVE_NSGL
#elif BX_PLATFORM_WINDOWS
#define GLFW_EXPOSE_NATIVE_WIN32
#define GLFW_EXPOSE_NATIVE_WGL
#endif

#include <GLFW/glfw3native.h>
#include "core/Core.hpp"

namespace Tina
{
    class GLFWWindow : public Window
    {
    protected:
        struct GlfwWindowDeleter
        {
            void operator()(GLFWwindow* window) const
            {
                if (window)
                {
                    glfwDestroyWindow(window);
                }     
            }
        };

    public:
        GLFWWindow();
        ~GLFWWindow() override = default;

        void create(WindowConfig config) override;
        void render() override;
        void destroy() override;
        void pollEvents() override;
        bool shouldClose() override;

        [[nodiscard]] GLFWwindow* getNativeWindow() const { return m_window.get(); }
        
    private:
        static void errorCallback(int error, const char* description);

    private:
        Scope<GLFWwindow, GlfwWindowDeleter> m_window;
    };
} // Tina

#endif //TINA_WINDOW_GLFWWINDOW_HPP
