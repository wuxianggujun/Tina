//
// Window.cpp - GLFW window wrapper.
//

#include "Window.hpp"
#include "../core/Log.hpp"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include <GLFW/glfw3.h>

#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#elif defined(__APPLE__)
#define GLFW_EXPOSE_NATIVE_COCOA
#elif defined(__linux__)
#define GLFW_EXPOSE_NATIVE_X11
#if defined(TINA_GLFW_ENABLE_WAYLAND)
#define GLFW_EXPOSE_NATIVE_WAYLAND
#define TINA_GLFW_HAS_NATIVE_WAYLAND 1
#endif
#endif

#include <GLFW/glfw3native.h>

#include <algorithm>
#include <cstdint>

namespace Tina::Engine {
namespace {

int g_glfwRefCount = 0;

bool ensureGlfwInitialized()
{
    if (g_glfwRefCount > 0) {
        ++g_glfwRefCount;
        return true;
    }

    if (glfwInit() != GLFW_TRUE) {
        const char* description = nullptr;
        glfwGetError(&description);
        TINA_ERROR("glfwInit failed: {}", description ? description : "unknown error");
        return false;
    }

    g_glfwRefCount = 1;
    return true;
}

void releaseGlfw()
{
    if (g_glfwRefCount <= 0) {
        return;
    }

    --g_glfwRefCount;
    if (g_glfwRefCount == 0) {
        glfwTerminate();
    }
}

GLFWmonitor* primaryMonitor()
{
    return glfwGetPrimaryMonitor();
}

} // namespace

Window::Window() = default;

Window::~Window()
{
    destroy();
}

bool Window::create(const WindowDesc& desc)
{
    if (m_window) {
        TINA_ERROR("Window already created");
        return false;
    }

    if (!ensureGlfwInitialized()) {
        return false;
    }
    m_ownsGlfw = true;

    m_desc = desc;
    m_title = desc.title ? desc.title : "";
    m_windowedWidth = std::max(1, desc.width);
    m_windowedHeight = std::max(1, desc.height);

    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, desc.visible ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_RESIZABLE, desc.resizable ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_DECORATED, desc.borderless ? GLFW_FALSE : GLFW_TRUE);
    glfwWindowHint(GLFW_MAXIMIZED, desc.maximized ? GLFW_TRUE : GLFW_FALSE);

    GLFWmonitor* monitor = nullptr;
    int width = std::max(1, desc.width);
    int height = std::max(1, desc.height);

    if (desc.fullscreen) {
        monitor = primaryMonitor();
        if (monitor) {
            const GLFWvidmode* mode = glfwGetVideoMode(monitor);
            if (mode) {
                width = mode->width;
                height = mode->height;
            }
        }
    }

    m_window = glfwCreateWindow(width, height, m_title.c_str(), monitor, nullptr);
    if (!m_window) {
        const char* description = nullptr;
        glfwGetError(&description);
        TINA_ERROR("glfwCreateWindow failed: {}", description ? description : "unknown error");
        releaseGlfw();
        m_ownsGlfw = false;
        return false;
    }

    if (!desc.fullscreen) {
        if (desc.x != -1 && desc.y != -1) {
            glfwSetWindowPos(m_window, desc.x, desc.y);
        } else {
            center();
        }
        glfwGetWindowPos(m_window, &m_windowedX, &m_windowedY);
        glfwGetWindowSize(m_window, &m_windowedWidth, &m_windowedHeight);
    }

    TINA_INFO("Window created with GLFW: {}x{} '{}'", width, height, m_title.c_str());
    return true;
}

void Window::destroy()
{
    if (m_window) {
        glfwDestroyWindow(m_window);
        m_window = nullptr;
        TINA_INFO("Window destroyed");
    }

    if (m_ownsGlfw) {
        releaseGlfw();
        m_ownsGlfw = false;
    }
}

bool Window::isValid() const
{
    return m_window != nullptr;
}

void Window::setTitle(const char* title)
{
    m_title = title ? title : "";
    if (m_window) {
        glfwSetWindowTitle(m_window, m_title.c_str());
    }
}

const char* Window::getTitle() const
{
    return m_title.c_str();
}

void Window::setSize(int width, int height)
{
    if (!m_window) {
        return;
    }

    width = std::max(1, width);
    height = std::max(1, height);
    glfwSetWindowSize(m_window, width, height);
    m_desc.width = width;
    m_desc.height = height;

    if (!isFullscreen()) {
        m_windowedWidth = width;
        m_windowedHeight = height;
    }
}

void Window::getSize(int& width, int& height) const
{
    if (!m_window) {
        width = 0;
        height = 0;
        return;
    }

    glfwGetWindowSize(m_window, &width, &height);
}

int Window::getWidth() const
{
    int width = 0;
    int height = 0;
    getSize(width, height);
    return width;
}

int Window::getHeight() const
{
    int width = 0;
    int height = 0;
    getSize(width, height);
    return height;
}

void Window::getSizeInPixels(int& width, int& height) const
{
    getFramebufferSize(width, height);
}

void Window::getFramebufferSize(int& width, int& height) const
{
    if (!m_window) {
        width = 0;
        height = 0;
        return;
    }

    glfwGetFramebufferSize(m_window, &width, &height);
}

void Window::setPosition(int x, int y)
{
    if (!m_window || isFullscreen()) {
        return;
    }

    glfwSetWindowPos(m_window, x, y);
    m_windowedX = x;
    m_windowedY = y;
}

void Window::getPosition(int& x, int& y) const
{
    if (!m_window) {
        x = 0;
        y = 0;
        return;
    }

    glfwGetWindowPos(m_window, &x, &y);
}

void Window::show()
{
    if (!m_window) {
        return;
    }

    glfwShowWindow(m_window);
    m_desc.visible = true;
}

void Window::hide()
{
    if (!m_window) {
        return;
    }

    glfwHideWindow(m_window);
    m_desc.visible = false;
}

bool Window::isVisible() const
{
    return m_window && glfwGetWindowAttrib(m_window, GLFW_VISIBLE) == GLFW_TRUE;
}

void Window::minimize()
{
    if (m_window) {
        glfwIconifyWindow(m_window);
    }
}

void Window::maximize()
{
    if (!m_window) {
        return;
    }

    glfwMaximizeWindow(m_window);
    m_desc.maximized = true;
}

void Window::restore()
{
    if (!m_window) {
        return;
    }

    glfwRestoreWindow(m_window);
    m_desc.maximized = false;
}

bool Window::isMinimized() const
{
    return m_window && glfwGetWindowAttrib(m_window, GLFW_ICONIFIED) == GLFW_TRUE;
}

bool Window::isMaximized() const
{
    return m_window && glfwGetWindowAttrib(m_window, GLFW_MAXIMIZED) == GLFW_TRUE;
}

void Window::setFullscreen(bool fullscreen)
{
    if (!m_window || fullscreen == isFullscreen()) {
        return;
    }

    if (fullscreen) {
        glfwGetWindowPos(m_window, &m_windowedX, &m_windowedY);
        glfwGetWindowSize(m_window, &m_windowedWidth, &m_windowedHeight);

        GLFWmonitor* monitor = primaryMonitor();
        const GLFWvidmode* mode = monitor ? glfwGetVideoMode(monitor) : nullptr;
        if (monitor && mode) {
            glfwSetWindowMonitor(
                m_window,
                monitor,
                0,
                0,
                mode->width,
                mode->height,
                mode->refreshRate
            );
        }
    } else {
        glfwSetWindowMonitor(
            m_window,
            nullptr,
            m_windowedX,
            m_windowedY,
            std::max(1, m_windowedWidth),
            std::max(1, m_windowedHeight),
            GLFW_DONT_CARE
        );
    }

    m_desc.fullscreen = fullscreen;
}

bool Window::isFullscreen() const
{
    return m_window && glfwGetWindowMonitor(m_window) != nullptr;
}

void Window::toggleFullscreen()
{
    setFullscreen(!isFullscreen());
}

void Window::setResizable(bool resizable)
{
    if (m_window) {
        glfwSetWindowAttrib(m_window, GLFW_RESIZABLE, resizable ? GLFW_TRUE : GLFW_FALSE);
    }
    m_desc.resizable = resizable;
}

bool Window::isResizable() const
{
    return m_window && glfwGetWindowAttrib(m_window, GLFW_RESIZABLE) == GLFW_TRUE;
}

void Window::setBorderless(bool borderless)
{
    if (m_window) {
        glfwSetWindowAttrib(m_window, GLFW_DECORATED, borderless ? GLFW_FALSE : GLFW_TRUE);
    }
    m_desc.borderless = borderless;
}

bool Window::isBorderless() const
{
    return m_window && glfwGetWindowAttrib(m_window, GLFW_DECORATED) == GLFW_FALSE;
}

void Window::focus()
{
    if (m_window) {
        glfwFocusWindow(m_window);
    }
}

bool Window::hasFocus() const
{
    return m_window && glfwGetWindowAttrib(m_window, GLFW_FOCUSED) == GLFW_TRUE;
}

void* Window::getNativeHandle() const
{
    if (!m_window) {
        return nullptr;
    }

#if defined(_WIN32)
    return glfwGetWin32Window(m_window);
#elif defined(__APPLE__)
    return glfwGetCocoaWindow(m_window);
#elif defined(__linux__)
#if defined(TINA_GLFW_HAS_NATIVE_WAYLAND)
    if (glfwGetPlatform() == GLFW_PLATFORM_WAYLAND) {
        return glfwGetWaylandWindow(m_window);
    }
#endif
    return reinterpret_cast<void*>(static_cast<uintptr_t>(glfwGetX11Window(m_window)));
#else
    return nullptr;
#endif
}

void* Window::getNativeDisplayHandle() const
{
    if (!m_window) {
        return nullptr;
    }

#if defined(__linux__)
#if defined(TINA_GLFW_HAS_NATIVE_WAYLAND)
    if (glfwGetPlatform() == GLFW_PLATFORM_WAYLAND) {
        return glfwGetWaylandDisplay();
    }
#endif
    return glfwGetX11Display();
#else
    return nullptr;
#endif
}

bool Window::usesWayland() const
{
#if defined(__linux__) && defined(TINA_GLFW_HAS_NATIVE_WAYLAND)
    return m_window && glfwGetPlatform() == GLFW_PLATFORM_WAYLAND;
#else
    return false;
#endif
}

void Window::center()
{
    if (!m_window || isFullscreen()) {
        return;
    }

    GLFWmonitor* monitor = primaryMonitor();
    if (!monitor) {
        return;
    }

    int monitorX = 0;
    int monitorY = 0;
    int monitorWidth = 0;
    int monitorHeight = 0;
    glfwGetMonitorWorkarea(monitor, &monitorX, &monitorY, &monitorWidth, &monitorHeight);

    int windowWidth = 0;
    int windowHeight = 0;
    glfwGetWindowSize(m_window, &windowWidth, &windowHeight);

    const int x = monitorX + (monitorWidth - windowWidth) / 2;
    const int y = monitorY + (monitorHeight - windowHeight) / 2;
    glfwSetWindowPos(m_window, x, y);
    m_windowedX = x;
    m_windowedY = y;
}

void Window::flash()
{
    if (m_window) {
        glfwRequestWindowAttention(m_window);
    }
}

void Window::setOpacity(float opacity)
{
    if (m_window) {
        glfwSetWindowOpacity(m_window, std::clamp(opacity, 0.0f, 1.0f));
    }
}

void Window::pollEvents()
{
    glfwPollEvents();
}

bool Window::shouldClose() const
{
    return m_window && glfwWindowShouldClose(m_window) == GLFW_TRUE;
}

void Window::requestClose()
{
    if (m_window) {
        glfwSetWindowShouldClose(m_window, GLFW_TRUE);
    }
}

const char* Window::getClipboardText() const
{
    return m_window ? glfwGetClipboardString(m_window) : nullptr;
}

void Window::setClipboardText(const char* text)
{
    if (m_window) {
        glfwSetClipboardString(m_window, text ? text : "");
    }
}

} // namespace Tina::Engine
