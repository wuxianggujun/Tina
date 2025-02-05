//
// Created by wuxianggujun on 2025/2/5.
//

#include "tina/window/WindowManager.hpp"
#include "tina/log/Logger.hpp"
#include "tina/event/EventQueue.hpp"
#include <bgfx/bgfx.h>
#include "tina/core/Context.hpp"

namespace Tina
{
    static void keyCallback(GLFWwindow* glfwWindow, int32_t key, int32_t scancode, int32_t action, int32_t mods);
    static void charCallback(GLFWwindow* glfwWindow, uint32_t codepoint);
    static void scrollCallback(GLFWwindow* glfwWindow, double dx, double dy);
    static void cursorPosCallback(GLFWwindow* glfwWindow, double mx, double my);
    static void mouseButtonCallback(GLFWwindow* glfwWindow, int32_t button, int32_t action, int32_t mods);
    static void windowSizeCallback(GLFWwindow* glfwWindow, int32_t width, int32_t height);
    static void dropFileCallback(GLFWwindow* glfwWindow, int32_t count, const char** filePaths);
    static void joystickCallback(int jid, int action);

    WindowManager* WindowManager::s_instance = nullptr;

    WindowManager::WindowManager(Context* context): m_context(context), m_windowHandleAlloc()
    {
        s_instance = this;
    }

    WindowManager::~WindowManager()
    {
        s_instance = nullptr;
        terminate();
    }

    bool WindowManager::initialize()
    {
        glfwSetErrorCallback(errorCb);
        if (!glfwInit())
        {
            TINA_LOG_ERROR("WindowManager::initialize", " Failed to initialize GLFW");
            return false;
        }

        // 设置手柄回掉
        glfwSetJoystickCallback(joystickCallback);
        glfwWindowHint(GLFW_CLIENT_API,GLFW_NO_API);
        TINA_LOG_INFO("WindowManager::initialize", "GLFW initialized successfully.");
        return true;
    }

    void WindowManager::terminate()
    {
        for (auto const& [handle,window] : m_windowMap)
        {
            delete window;
        }

        m_windowMap.clear();
        m_windowHandleAlloc.reset();

        glfwTerminate();
    }

    WindowHandle WindowManager::createWindow(int32_t x, int32_t y, int32_t width, int32_t height, uint32_t flags,
                                             const char* title)
    {
        Window::WindowConfig windowConfig;
        windowConfig.title = String(title);
        windowConfig.width = width;
        windowConfig.height = height;

        WindowHandle handle = {m_windowHandleAlloc.alloc()};
        // 创建Window对象，传入WindowManager指针和WindowHandle
        auto* window = new Window(this, handle, windowConfig);
        // 检查GLFW窗口是否创建成功
        if (!window->getHandle())
        {
            m_windowHandleAlloc.free(handle.idx);
            delete window;
            return {UINT16_MAX}; // 无效的WindowHandle
        }

        m_windowMap[handle] = window;

        glfwSetWindowPos(window->getHandle(), x, y);

        // 设置GLFW回掉函数
        glfwSetKeyCallback(window->getHandle(), keyCallback);
        glfwSetCharCallback(window->getHandle(), charCallback);
        glfwSetScrollCallback(window->getHandle(), scrollCallback);
        glfwSetCursorPosCallback(window->getHandle(), cursorPosCallback);
        glfwSetMouseButtonCallback(window->getHandle(), mouseButtonCallback);
        glfwSetWindowSizeCallback(window->getHandle(), windowSizeCallback);
        glfwSetDropCallback(window->getHandle(), dropFileCallback);

        // 发送窗口创建事件
        Event event;
        event.type = Event::WindowCreate;
        event.windowHandle = handle;
        event.window.nativeWindowHandle = getNativeWindowHandle(handle);
        m_context->getEventQueue().postEvent(event);

        TINA_LOG_INFO("WindowManager::createWindow", "Window created successfully. Handle: {}", handle.idx);
        return handle;
    }

    void WindowManager::destroyWindow(WindowHandle handle)
    {
        auto it = m_windowMap.find(handle);

        if (it != m_windowMap.end())
        {
            Window* window = it->second;
            // 发送窗口销毁事件
            Event event;
            event.type = Event::WindowDestroy;
            event.windowHandle = handle;
            event.window.nativeWindowHandle = nullptr;
            m_context->getEventQueue().postEvent(event);

            delete window;
            m_windowMap.erase(it);
            m_windowHandleAlloc.free(handle.idx);
        }
    }

    Window* WindowManager::getWindow(WindowHandle handle)
    {
        auto it = m_windowMap.find(handle);
        if (it != m_windowMap.end())
        {
            return it->second;
        }
        return nullptr;
    }

    void WindowManager::processMessage()
    {
        glfwPollEvents();
    }

    void* WindowManager::getNativeWindowHandle(WindowHandle handle)
    {
        Window* window = getWindow(handle);
        if (window)
        {
#if BX_PLATFORM_LINUX || BX_PLATFORM_BSD
            return (void*)(uintptr_t)glfwGetX11Window(window->getHandle());
#elif BX_PLATFORM_OSX
            return glfwGetCocoaWindow(window->getHandle());
#elif BX_PLATFORM_WINDOWS
            return glfwGetWin32Window(window->getHandle());
#endif
        }
        return nullptr;
    }

    void* WindowManager::getNativeDisplayHandle()
    {
#if BX_PLATFORM_LINUX || BX_PLATFORM_BSD
        return glfwGetX11Display();
#elif BX_PLATFORM_OSX
        return NULL;
#elif BX_PLATFORM_WINDOWS
        return NULL;
#endif
    }

    bgfx::NativeWindowHandleType::Enum WindowManager::getNativeWindowHandleType()
    {
#if BX_PLATFORM_LINUX || BX_PLATFORM_BSD
        return bgfx::NativeWindowHandleType::X11;
#elif BX_PLATFORM_OSX
        return bgfx::NativeWindowHandleType::Cocoa;
#elif BX_PLATFORM_WINDOWS
        return bgfx::NativeWindowHandleType::Default;
#endif
    }

    WindowHandle WindowManager::findHandle(GLFWwindow* glfwWindow) const
    {
        for (auto const& [handle, window] : m_windowMap)
        {
            if (window->getHandle() == glfwWindow)
            {
                return handle;
            }
        }
        return {UINT16_MAX};
    }

    void WindowManager::errorCb(int _error, const char* _description)
    {
        TINA_LOG_ERROR("GLFW Error", "Code: {}, Description: {}", _error, _description);
    }

    void WindowManager::eventCallback_key(WindowHandle handle, GLFWwindow* glfwWindow, int32_t key, int32_t scancode,
                                          int32_t action, int32_t mods)
    {
        Event event;
        event.type = Event::Key;
        event.windowHandle = handle;
        event.key.key = key;
        event.key.scancode = scancode;
        event.key.action = action;
        event.key.mods = mods;
        m_context->getEventQueue().postEvent(event);
    }

    void WindowManager::eventCallback_char(WindowHandle handle, GLFWwindow* glfwWindow, uint32_t codepoint)
    {
        Event event;
        event.type = Event::Char;
        event.windowHandle = handle;
        event.character.codepoint = codepoint;
        m_context->getEventQueue().postEvent(event);
    }

    void WindowManager::eventCallback_scroll(WindowHandle handle, GLFWwindow* glfwWindow, double dx, double dy)
    {
        Event event;
        event.type = Event::MouseScroll;
        event.windowHandle = handle;
        event.mouseScroll.xoffset = dx;
        event.mouseScroll.yoffset = dy;
        m_context->getEventQueue().postEvent(event);
    }

    void WindowManager::eventCallback_cursorPos(WindowHandle handle, GLFWwindow* glfwWindow, double mx, double my)
    {
        Event event;
        event.type = Event::CursorPos;
        event.windowHandle = handle;
        event.cursorPos.x = mx;
        event.cursorPos.y = my;
        m_context->getEventQueue().postEvent(event);
    }

    void WindowManager::eventCallback_mouseButton(WindowHandle handle, GLFWwindow* glfwWindow, int32_t button,
                                                  int32_t action, int32_t mods)
    {
        Event event;
        event.type = Event::MouseButton;
        event.windowHandle = handle;
        event.mouseButton.button = button;
        event.mouseButton.action = action;
        event.mouseButton.mods = mods;
        m_context->getEventQueue().postEvent(event);
    }

    void WindowManager::eventCallback_windowSize(WindowHandle handle, GLFWwindow* glfwWindow, int32_t width,
                                                 int32_t height)
    {
        Event event;
        event.type = Event::WindowResize;
        event.windowHandle = handle;
        event.windowResize.width = width;
        event.windowResize.height = height;
        m_context->getEventQueue().postEvent(event);
    }

    void WindowManager::eventCallback_dropFile(WindowHandle handle, GLFWwindow* glfwWindow, int32_t count,
                                               const char** filePaths)
    {
        Event event;
        event.type = Event::DropFile;
        event.windowHandle = handle;
        event.dropFile.count = count;
        event.dropFile.paths = filePaths;
        m_context->getEventQueue().postEvent(event);
    }

    void WindowManager::eventCallback_joystick(int jid, int action)
    {
        Event event;
        event.type = Event::Gamepad;
        event.windowHandle = {UINT16_MAX}; // 手柄事件不关联特定窗口
        event.gamepad.jid = jid;
        event.gamepad.action = action;
        m_context->getEventQueue().postEvent(event);
    }

    static void keyCallback(GLFWwindow* glfwWindow, int32_t key, int32_t scancode, int32_t action, int32_t mods)
    {
        if (auto* manager = WindowManager::getInstance())
        {
            const WindowHandle handle = manager->findHandle(glfwWindow);
            if (handle.idx != UINT16_MAX)
            {
                manager->eventCallback_key(handle, glfwWindow, key, scancode, action, mods);
            }
        }
    }

    static void charCallback(GLFWwindow* glfwWindow, uint32_t codepoint)
    {
        if (auto* manager = WindowManager::getInstance())
        {
            WindowHandle handle = manager->findHandle(glfwWindow);
            if (handle.idx != UINT16_MAX)
            {
                manager->eventCallback_char(handle, glfwWindow, codepoint);
            }
        }
    }

    static void scrollCallback(GLFWwindow* glfwWindow, double dx, double dy)
    {
        if (auto* manager = WindowManager::getInstance())
        {
            WindowHandle handle = manager->findHandle(glfwWindow);
            if (handle.idx != UINT16_MAX)
            {
                manager->eventCallback_scroll(handle, glfwWindow, dx, dy);
            }
        }
    }

    static void cursorPosCallback(GLFWwindow* glfwWindow, double mx, double my)
    {
        if (auto* manager = WindowManager::getInstance())
        {
            WindowHandle handle = manager->findHandle(glfwWindow);
            if (handle.idx != UINT16_MAX)
            {
                manager->eventCallback_cursorPos(handle, glfwWindow, mx, my);
            }
        }
    }

    static void mouseButtonCallback(GLFWwindow* glfwWindow, int32_t button, int32_t action, int32_t mods)
    {
        if (auto* manager = WindowManager::getInstance())
        {
            WindowHandle handle = manager->findHandle(glfwWindow);
            if (handle.idx != UINT16_MAX)
            {
                manager->eventCallback_mouseButton(handle, glfwWindow, button, action, mods);
            }
        }
    }

    static void windowSizeCallback(GLFWwindow* glfwWindow, int32_t width, int32_t height)
    {
        if (auto* manager = WindowManager::getInstance())
        {
            WindowHandle handle = manager->findHandle(glfwWindow);
            if (handle.idx != UINT16_MAX)
            {
                manager->eventCallback_windowSize(handle, glfwWindow, width, height);
            }
        }
    }

    static void dropFileCallback(GLFWwindow* glfwWindow, int32_t count, const char** filePaths)
    {
        if (auto* manager = WindowManager::getInstance())
        {
            WindowHandle handle = manager->findHandle(glfwWindow);
            if (handle.idx != UINT16_MAX)
            {
                manager->eventCallback_dropFile(handle, glfwWindow, count, filePaths);
            }
        }
    }

    static void joystickCallback(int jid, int action)
    {
        if (auto* manager = WindowManager::getInstance())
        {
            manager->eventCallback_joystick(jid, action);
        }
    }
} // Tina
