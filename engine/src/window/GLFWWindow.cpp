//
// Created by wuxianggujun on 2024/7/12.
//

#include "GLFWWindow.hpp"

namespace Tina {
    GLFWWindow::GLFWWindow() : m_window(nullptr, GlfwWindowDeleter()) {
        glfwSetErrorCallback(errorCallback);
        if (!glfwInit()) {
            printf("GLFW initialization failed\n");
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
            printf("GLFW window creation failed\n");
        }

        bgfx::Init bgfxInit;
        bgfxInit.type = bgfx::RendererType::Count;
        bgfxInit.resolution.width = config.size.width;
        bgfxInit.resolution.height = config.size.height;
        bgfxInit.resolution.reset = BGFX_RESET_VSYNC;
        bgfxInit.callback = &m_bgfxCallback;

#if BX_PLATFORM_LINUX || BX_PLATFORM_BSD
        bgfxInit.platformData.nwh = (void*)(uintptr_t) glfwGetX11Window(m_window.get());
        bgfxInit.platformData.ndt = glfwGetX11Display();
#elif BX_PLATFORM_OSX
        bgfxInit.platformData.nwh = glfwGetCocoaWindow(m_window.get());
#elif BX_PLATFORM_WINDOWS
        bgfxInit.platformData.nwh = glfwGetWin32Window(m_window.get());
#endif

        if (!bgfx::init(bgfxInit)) {
            printf("Bgfx initialization failed\n");
            return;
        }
        bgfx::setDebug(BGFX_DEBUG_NONE);
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
        printf("GLFW Error (%d): %s\n", error, description);
    }
} // Tina
