//
// Created by wuxianggujun on 2024/7/12.
//

#include "GLFWWindow.hpp"
#include <fmt/printf.h>

#include "EventHandler.hpp"

namespace Tina {
    GLFWWindow::GLFWWindow() : m_window(nullptr, GlfwWindowDeleter()) {
        glfwSetErrorCallback(errorCallback);
        if (!glfwInit()) {
            fmt::printf("GLFW initialization failed\n");
            return;
        }
    }

    void GLFWWindow::create(const WindowConfig &config) {
#if defined(GLFW_EXPOSE_NATIVE_WIN32)
        if (glfwPlatformSupported(GLFW_PLATFORM_WIN32))
            glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_WIN32);
#elif defined (GLFW_EXPOSE_NATIVE_COCOA)
         if (glfwPlatformSupported(GLFW_PLATFORM_COCOA))
            glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_COCOA);
#elif defined(GLFW_EXPOSE_NATIVE_WAYLAND)
        if (glfwPlatformSupported(GLFW_PLATFORM_WAYLAND))
            glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_WAYLAND);
#elif  defined(GLFW_EXPOSE_NATIVE_X11)
        if (glfwPlatformSupported(GLFW_PLATFORM_X11))
            glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
#endif

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, config.resizable ? GLFW_TRUE : GLFW_FALSE);
        
        m_windowSize = config.resolution;

        m_window.reset(
            glfwCreateWindow(m_windowSize.width, m_windowSize.height, config.title.c_str(), nullptr, nullptr));

        if (!m_window) {
            fmt::print("Failed to create GLFW window\n");
            return;
        }

        bgfx::Init bgfxInit;
#if TINA_PLATFORM_WINDOWS
        bgfxInit.type = bgfx::RendererType::Direct3D11;
#else
        bgfxInit.type = bgfx::RendererType::OpenGL;
#endif
        bgfxInit.resolution.width = m_windowSize.width;
        bgfxInit.resolution.height = m_windowSize.height;
        bgfxInit.resolution.reset = BGFX_RESET_VSYNC;
        bgfxInit.callback = &m_bgfxCallback;

        bgfxInit.platformData.nwh = glfwNativeWindowHandle(m_window.get());
        bgfxInit.platformData.ndt = getNativeDisplayHandle();
        bgfxInit.platformData.type = getNativeWindowHandleType();
        bgfxInit.platformData.backBuffer = nullptr;
        bgfxInit.platformData.backBufferDS = nullptr;

        fmt::print("Initializing BGFX...\n");
        fmt::print("Window handle: {}\n", (void*)bgfxInit.platformData.nwh);
        fmt::print("Display handle: {}\n", (void*)bgfxInit.platformData.ndt);
        fmt::print("Window handle type: {}\n", static_cast<int>(bgfxInit.platformData.type));

        if (!bgfx::init(bgfxInit)) {
            fmt::print("BGFX initialization failed\n");
            return;
        }
        fmt::print("BGFX initialized successfully\n");

        // Set debug flags and text size
        bgfx::setDebug(BGFX_DEBUG_TEXT | BGFX_DEBUG_STATS);

        // 设置视口
        bgfx::setViewRect(0, 0, 0, uint16_t(m_windowSize.width), uint16_t(m_windowSize.height));
        bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x303030ff, 1.0f, 0);

        glfwSetWindowUserPointer(m_window.get(), this);
        glfwSetWindowSizeCallback(m_window.get(), windowSizeCallBack);
        glfwSetKeyCallback(m_window.get(), keyboardCallback);

        fmt::print("Window creation completed\n");
    }

    void GLFWWindow::setEventHandler(ScopePtr<EventHandler> &&eventHandler) {
        m_eventHandle = std::move(eventHandler);
    }

    void GLFWWindow::destroy() {
        m_window.reset();
        glfwTerminate();
    }

    void GLFWWindow::pollEvents() {
        glfwPollEvents();
    }

    bool GLFWWindow::shouldClose() {
        return glfwWindowShouldClose(m_window.get());
    }

    void GLFWWindow::saveScreenShot(const std::string &fileName) {
        bgfx::requestScreenShot(BGFX_INVALID_HANDLE, fileName.c_str());
    }

    void GLFWWindow::windowSizeCallBack(GLFWwindow *window, int width, int height) {
        auto *self = static_cast<GLFWWindow *>(glfwGetWindowUserPointer(window));
        if (self) {
            self->m_windowSize.width = width;
            self->m_windowSize.height = height;
            bgfx::reset(width, height, BGFX_RESET_VSYNC);
        }
    }

    void GLFWWindow::keyboardCallback(GLFWwindow *window, int32_t key, int32_t scancode, int32_t action,
                                      int32_t mods) {
        const auto *self = static_cast<GLFWWindow *>(glfwGetWindowUserPointer(window));
        if (self && self->m_eventHandle) {
            KeyboardEvent event(key, scancode, action, mods);
            self->m_eventHandle->emplaceEvent<KeyboardEvent>(event);
        }
    }

    void *GLFWWindow::glfwNativeWindowHandle(GLFWwindow *_window) {
#	if TINA_PLATFORM_LINUX
        if (GLFW_PLATFORM_WAYLAND == glfwGetPlatform()) {
            return glfwGetWaylandWindow(_window);
        }
        return reinterpret_cast<void *>(glfwGetX11Window(_window));
#	elif TINA_PLATFORM_OSX
        return glfwGetCocoaWindow(_window);
#	elif TINA_PLATFORM_WINDOWS
        return glfwGetWin32Window(_window);
#	endif
    }

    void *GLFWWindow::getNativeDisplayHandle() {
#if TINA_PLATFORM_LINUX
        if (GLFW_PLATFORM_WAYLAND == glfwGetPlatform()) {
            return glfwGetWaylandDisplay();
        }
        return glfwGetX11Display();
#	else
        return nullptr;
#	endif
    }

    bgfx::NativeWindowHandleType::Enum GLFWWindow::getNativeWindowHandleType() {
#if TINA_PLATFORM_LINUX
        if (GLFW_PLATFORM_WAYLAND == glfwGetPlatform()) {
            return bgfx::NativeWindowHandleType::Wayland;
        }
#endif
        return bgfx::NativeWindowHandleType::Default;
    }

    void GLFWWindow::errorCallback(int error, const char *description) {
        fmt::printf("GLFW Error (%d): %s\n", error, description);
    }

    void GLFWWindow::setTitle(const std::string& title) {
        if (m_window) {
            glfwSetWindowTitle(m_window.get(), title.c_str());
            m_title = title;
        }
    }

    void GLFWWindow::setSize(const Vector2i& size) {
        if (m_window) {
            glfwSetWindowSize(m_window.get(), size.width, size.height);
            m_windowSize = size;
            bgfx::reset(size.width, size.height, m_isVSync ? BGFX_RESET_VSYNC : BGFX_RESET_NONE);
        }
    }

    void GLFWWindow::setVSync(bool enabled) {
        if (m_isVSync != enabled) {
            m_isVSync = enabled;
            bgfx::reset(m_windowSize.width, m_windowSize.height, 
                enabled ? BGFX_RESET_VSYNC : BGFX_RESET_NONE);
        }
    }

    void GLFWWindow::setFullscreen(bool fullscreen) {
        if (m_window && m_isFullscreen != fullscreen) {
            if (fullscreen) {
                // 获取主显示器
                GLFWmonitor* monitor = glfwGetPrimaryMonitor();
                if (monitor) {
                    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
                    glfwSetWindowMonitor(m_window.get(), monitor, 0, 0, 
                        mode->width, mode->height, mode->refreshRate);
                }
            } else {
                // 恢复窗口模式
                glfwSetWindowMonitor(m_window.get(), nullptr, 100, 100, 
                    m_windowSize.width, m_windowSize.height, 0);
            }
            m_isFullscreen = fullscreen;
        }
    }

    std::string GLFWWindow::getTitle() const {
        return m_title;
    }

    bool GLFWWindow::isFullscreen() const {
        return m_isFullscreen;
    }

    bool GLFWWindow::isVSync() const {
        return m_isVSync;
    }

    bool GLFWWindow::isVisible() const {
        return m_window && glfwGetWindowAttrib(m_window.get(), GLFW_VISIBLE);
    }
} // Tina
