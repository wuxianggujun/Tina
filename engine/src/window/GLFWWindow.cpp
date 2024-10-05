//
// Created by wuxianggujun on 2024/7/12.
//

#include "GLFWWindow.hpp"
#include <fmt/printf.h>

namespace Tina {
    GLFWWindow::GLFWWindow() : m_window(nullptr, GlfwWindowDeleter()) {
        glfwSetErrorCallback(errorCallback);
        if (!glfwInit()) {
            fmt::printf("GLFW initialization failed\n");
            return;
        }
    }

    void GLFWWindow::create(WindowConfig config) {
#if defined(GLFW_EXPOSE_NATIVE_WIN32)
        if (glfwPlatformSupported(GLFW_PLATFORM_WIN32))
            glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_WIN32);
#elif defined (GLFW_EXPOSE_NATIVE_COCOA)
         if (glfwPlatformSupported(GLFW_PLATFORM_COCOA))
            glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_COCOA);
#elif  defined(GLFW_EXPOSE_NATIVE_X11)
        if (glfwPlatformSupported(GLFW_PLATFORM_X11))
            glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
#elif defined(TINA_CONFIG_USE_WAYLAND)
        if (glfwPlatformSupported(GLFW_PLATFORM_WAYLAND))
            glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_WAYLAND);
#endif

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        m_window.reset(glfwCreateWindow(config.size.width, config.size.height, config.title, nullptr, nullptr));

        bgfx::Init bgfxInit;
        bgfxInit.type = bgfx::RendererType::Vulkan;
        bgfxInit.vendorId = BGFX_PCI_ID_NONE;
        bgfxInit.resolution.width = config.size.width;
        bgfxInit.resolution.height = config.size.height;
        bgfxInit.resolution.reset = BGFX_RESET_VSYNC;
        bgfxInit.callback = &m_bgfxCallback;

        // bgfxInit.platformData.context = glfwGetCurrentContext();
        bgfxInit.platformData.nwh = glfwNativeWindowHandle(m_window.get());
        bgfxInit.platformData.ndt = getNativeDisplayHandle();
        bgfxInit.platformData.type = getNativeWindowHandleType();

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

    void *GLFWWindow::glfwNativeWindowHandle(GLFWwindow *window) {
#if TINA_PLATFORM_LINUX
        if (glfwGetPlatform() == GLFW_PLATFORM_WAYLAND) {
            return glfwGetWaylandWindow(window);
        }
        else  {
            return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(getWindowX11Window(window)));
        }
#elif TINA_PLATFORM_OSX
        return glfwGetCocoaWindow(window);
#elif TINA_PLATFORM_WINDOWS
        return glfwGetWin32Window(window);
#endif
        return nullptr;
    }

    void *GLFWWindow::getNativeDisplayHandle() {
#if TINA_PLATFORM_LINUX
        if (glfwGetPlatform() == GLFW_PLATFORM_WAYLAND)
        {
            return glfwGetWaylandDisplay();
        }
        else
        {
            return glfwGetX11Display();
        }
#	else
        return nullptr;
#	endif
    }

    bgfx::NativeWindowHandleType::Enum GLFWWindow::getNativeWindowHandleType() {
#if TINA_PLATFORM_LINUX
		if (glfwGetPlatform() == GLFW_PLATFORM_WAYLAND)
        {
            return bgfx::NativeWindowHandleType::Wayland;
        }
    else
        {
     return bgfx::NativeWindowHandleType::Default;
     }
#	else
        return bgfx::NativeWindowHandleType::Default;
#	endif
    }


    void GLFWWindow::errorCallback(int error, const char *description) {
        fmt::printf("GLFW Error (%d): %s\n", error, description);
    }
} // Tina
