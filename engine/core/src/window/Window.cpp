//
// Created by wuxianggujun on 2025/2/5.
//

#include <utility>
#include <bx/handlealloc.h>
#include <bx/allocator.h>
#include "tina/window/Window.hpp"
#include "tina/window/WindowManager.hpp"
#include "tina/log/Log.hpp"
#include "tina/window/GlfwMemoryManager.hpp"
#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#if BX_PLATFORM_WINDOWS
    #define GLFW_EXPOSE_NATIVE_WIN32
#elif BX_PLATFORM_LINUX
    #define GLFW_EXPOSE_NATIVE_X11
#elif BX_PLATFORM_OSX
    #define GLFW_EXPOSE_NATIVE_COCOA
#endif
#include <GLFW/glfw3native.h>

namespace Tina
{
    Window::Window(WindowManager* windowManager, WindowHandle handle)
        : m_windowManager(windowManager), m_windowHandle(handle), m_handle(nullptr)
    {
    }

    bool Window::create(const WindowConfig& config)
    {
        size_t beforeMem = GlfwMemoryManager::getCurrentAllocated();

        // 验证WindowHandle
        if (!isValid(m_windowHandle)) {
            TINA_ENGINE_ERROR("Invalid window handle in Window::create");
            return false;
        }

        // 创建窗口
        m_handle = glfwCreateWindow(config.width, config.height, config.title,
                                    config.fullscreen ? glfwGetPrimaryMonitor() : nullptr, nullptr);

        if (!m_handle)
        {
            TINA_ENGINE_ERROR("Failed to create GLFW window");
            return false;
        }

        TINA_ENGINE_DEBUG("GLFW window created with handle: {}", (void*)m_handle);

        // 设置窗口用户指针
        glfwSetWindowUserPointer(m_handle, this);

        // 获取native handles
        auto handles = m_windowManager->getNativeHandles(m_windowHandle);
        
        TINA_ENGINE_DEBUG("Native handles - Window: {}, Display: {}", 
            (void*)handles.windowHandle, 
            (void*)handles.displayHandle);

        if (!handles.windowHandle) {
            TINA_ENGINE_ERROR("Failed to get native window handle");
            glfwDestroyWindow(m_handle);
            m_handle = nullptr;
            return false;
        }

        // 初始化bgfx
        bgfx::PlatformData pd{};
        pd.nwh = handles.windowHandle;
        pd.ndt = handles.displayHandle;
        bgfx::setPlatformData(pd);

        bgfx::Init init;
        init.type = bgfx::RendererType::Count; // 自动选择渲染器
        init.resolution.width = config.width;
        init.resolution.height = config.height;
        init.resolution.reset = config.vsync ? BGFX_RESET_VSYNC : BGFX_RESET_NONE;
        init.platformData = pd;
        init.debug = true;
        init.profile = true;

        TINA_ENGINE_INFO("Initializing bgfx with {}x{} resolution", config.width, config.height);
        
        if (!bgfx::init(init))
        {
            TINA_ENGINE_ERROR("Failed to initialize bgfx");
            glfwDestroyWindow(m_handle);
            m_handle = nullptr;
            return false;
        }

        // 设置调试标志
        bgfx::setDebug(BGFX_DEBUG_TEXT);

        // 设置视口
        bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x443355FF, 1.0f, 0);
        bgfx::setViewRect(0, 0, 0, config.width, config.height);

        size_t afterMem = GlfwMemoryManager::getCurrentAllocated();
        TINA_ENGINE_DEBUG("Window creation memory usage: {} bytes", afterMem - beforeMem);
        TINA_ENGINE_DEBUG("Window created: {}x{}, VSync: {}, Fullscreen: {}",
                          config.width, config.height, config.vsync, config.fullscreen);
        
        return true;
    }

    Window::~Window()
    {
        if (m_handle)
        {
            TINA_ENGINE_DEBUG("Destroying window");
            glfwSetWindowUserPointer(m_handle, nullptr);
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

    void Window::update()
    {
        // 这里可以添加窗口更新逻辑
    }

    void Window::close()
    {
        glfwSetWindowShouldClose(m_handle, GLFW_TRUE);
    }

    bool Window::isKeyPressed(int32_t key)
    {
        GLFWwindow* window = glfwGetCurrentContext();
        if (!window) return false;
        return glfwGetKey(window, key) == GLFW_PRESS;
    }

    bool Window::isMouseButtonPressed(int32_t button)
    {
        GLFWwindow* window = glfwGetCurrentContext();
        if (!window) return false;
        return glfwGetMouseButton(window, button) == GLFW_PRESS;
    }

    void Window::getMousePosition(double& x, double& y)
    {
        GLFWwindow* window = glfwGetCurrentContext();
        if (!window)
        {
            x = y = 0.0;
            return;
        }
        glfwGetCursorPos(window, &x, &y);
    }

    void Window::setMousePosition(double x, double y)
    {
        GLFWwindow* window = glfwGetCurrentContext();
        if (!window) return;
        glfwSetCursorPos(window, x, y);
    }

    void Window::setMouseCursor(bool visible)
    {
        GLFWwindow* window = glfwGetCurrentContext();
        if (!window) return;
        glfwSetInputMode(window, GLFW_CURSOR, visible ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_HIDDEN);
    }
} // Tina
