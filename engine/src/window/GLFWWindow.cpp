//
// Created by wuxianggujun on 2024/7/12.
//

#include "GLFWWindow.hpp"

#include <fmt/printf.h>

#include "bgfx/platform.h"

namespace Tina {
    GLFWWindow::GLFWWindow() : m_window(nullptr, GlfwWindowDeleter()) {
        glfwSetErrorCallback(errorCallback);
        if (!glfwInit()) {
            fmt::printf("GLFW initialization failed\n");
            return;
        }
    }

    void GLFWWindow::create(WindowConfig config) {
#if BX_PLATFORM_LINUX
        glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
        /*if (glfwPlatformSupported(GLFW_PLATFORM_WAYLAND))
            glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_WAYLAND);*/
#endif

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        m_window.reset(glfwCreateWindow(config.size.width, config.size.height, config.title, nullptr, nullptr));
        if (!m_window) {
            fmt::printf("GLFW window creation failed\n");
        }

        bgfx::Init bgfxInit;
        bgfxInit.type = bgfx::RendererType::Count;
        bgfxInit.resolution.width = config.size.width;
        bgfxInit.resolution.height = config.size.height;
        bgfxInit.resolution.reset = BGFX_RESET_VSYNC;
        bgfxInit.callback = &m_bgfxCallback;

        bgfx::PlatformData pd;
        pd.context = glfwGetCurrentContext();
#if defined(GLFW_EXPOSE_NATIVE_WAYLAND)
        pd.nwh = glfwGetWaylandWindow(m_window.get());
        pd.ndt = glfwGetWaylandDisplay();
#elif defined(GLFW_EXPOSE_NATIVE_X11)
        pd.nwh = (void *) (uintptr_t) glfwGetX11Window(m_window.get());
        pd.ndt = glfwGetX11Display();
#elif defined(GLFW_EXPOSE_NATIVE_WIN32)
        pd.nwh = glfwGetWin32Window(m_window.get());
        pd.ndt = nullptr;
#elif defined(GLFW_EXPOSE_NATIVE_COCOA)
        pd.nwh = glfwGetCocoaWindow(m_window.get());
        pd.ndt = nullptr;
#endif

        bgfxInit.platformData = pd;

        /*#if BX_PLATFORM_LINUX || BX_PLATFORM_BSD
                bgfxInit.platformData.nwh = (void*)(uintptr_t) glfwGetX11Window(m_window.get());
                bgfxInit.platformData.ndt = glfwGetX11Display();        
        #elif BX_PLATFORM_OSX
                bgfxInit.platformData.nwh = glfwGetCocoaWindow(m_window.get());
        #elif BX_PLATFORM_WINDOWS
                bgfxInit.platformData.nwh = glfwGetWin32Window(m_window.get());
        #endif*/

        if (!bgfx::init(bgfxInit)) {
            fmt::printf("Bgfx initialization failed\n");
            return;
        }
        //bgfx::setDebug(BGFX_DEBUG_NONE);
        bgfx::reset(config.size.width, config.size.height,BGFX_RESET_VSYNC);
    }

    void GLFWWindow::render() {
        // TODO: 以后尝试看一下以后是否可以用来绘制ui
    }

    void GLFWWindow::destroy() {
        m_window.reset();
    }

    void GLFWWindow::pollEvents() {
        glfwPollEvents();
    }

    bool GLFWWindow::shouldClose() {
        return !glfwWindowShouldClose(m_window.get());
    }

    void GLFWWindow::saveScreenShot(const std::string &fileName) {
        bgfx::requestScreenShot(BGFX_INVALID_HANDLE, fileName.c_str());
    }

    void GLFWWindow::errorCallback(int error, const char *description) {
        fmt::printf("GLFW Error (%d): %s\n", error, description);
    }
} // Tina
