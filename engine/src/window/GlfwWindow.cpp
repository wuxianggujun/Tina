//
// Created by wuxianggujun on 2024/7/12.
//

#include "GLFWWindow.hpp"

namespace Tina
{
    GlfwWindow::GlfwWindow() : m_window(nullptr, GlfwWindowDeleter())
    {
        glfwSetErrorCallback(errorCallback);
        if (!glfwInit())
        {
            printf("GLFW initialization failed\n");
            return;
        }
    }


    void GlfwWindow::create(WindowConfig config)
    {
#if BX_PLATFORM_LINUX
        glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
        /*if (glfwPlatformSupported(GLFW_PLATFORM_WAYLAND))
            glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_WAYLAND);*/
#endif

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        m_window.reset(glfwCreateWindow(config.size.width, config.size.height, config.title, nullptr, nullptr));
        if (!m_window)
        {
            printf("GLFW window creation failed\n");
        }

        bgfx::Init bgfxInit;
        bgfxInit.type = bgfx::RendererType::Count;
        bgfxInit.resolution.width = config.size.width;
        bgfxInit.resolution.height = config.size.height;
        bgfxInit.resolution.reset = BGFX_RESET_VSYNC;

#if BX_PLATFORM_LINUX || BX_PLATFORM_BSD
        bgfxInit.platformData.nwh = (void*)(uintptr_t) glfwGetX11Window(m_window.get());
        bgfxInit.platformData.ndt = glfwGetX11Display();
#elif BX_PLATFORM_OSX
        bgfxInit.platformData.nwh = glfwGetCocoaWindow(m_window.get());
#elif BX_PLATFORM_WINDOWS
        bgfxInit.platformData.nwh = glfwGetWin32Window(m_window.get());
#endif
        if (!bgfx::init(bgfxInit))
        {
            printf("Bgfx initialization failed\n");
            return;
        }
        bgfx::reset(config.size.width, config.size.height,BGFX_RESET_VSYNC);
    }

    void GlfwWindow::render()
    {
        bgfx::setViewClear(0,BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x303030ff, 1.0f, 0);
        bgfx::setViewRect(0, 0, 0, bgfx::BackbufferRatio::Equal);

        bgfx::touch(0);

        bgfx::frame();
    }

    void GlfwWindow::destroy()
    {
        bgfx::shutdown();
        glfwDestroyWindow(m_window.get());
    }

    void GlfwWindow::pollEvents()
    {
        glfwPollEvents();
    }

    bool GlfwWindow::shouldClose()
    {
        return !glfwWindowShouldClose(m_window.get());
    }

    void GlfwWindow::errorCallback(int error, const char* description)
    {
        printf("GLFW Error (%d): %s\n", error, description);
    }
} // Tina
