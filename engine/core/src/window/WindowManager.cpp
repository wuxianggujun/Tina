//
// Created by wuxianggujun on 2025/2/5.
//

#include "tina/window/WindowManager.hpp"
#include "tina/log/Logger.hpp"
#include "tina/event/EventQueue.hpp"
#include <bgfx/bgfx.h>

#include "bgfx/platform.h"
#include "tina/core/Context.hpp"

namespace Tina
{
    WindowManager* WindowManager::s_instance = nullptr;

    WindowManager::WindowManager(Context* context)
        : m_context(context)
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
        glfwSetErrorCallback(errorCallback);
        if (!glfwInit())
        {
            TINA_LOG_ERROR("WindowManager::initialize", " Failed to initialize GLFW");
            return false;
        }

        glfwSetJoystickCallback(joystickCallback);
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        TINA_LOG_INFO("WindowManager::initialize", "GLFW initialized successfully.");
        return true;
    }

    void WindowManager::terminate()
    {
        TINA_LOG_INFO("Terminating WindowManager");

        try {
            // 检查GLFW是否已初始化
            if (!glfwInit()) {
                TINA_LOG_DEBUG("GLFW not initialized, skipping cleanup");
                return;
            }

            // 先移除所有回调
            for (const auto& [idx, window] : m_windowMap) {
                if (GLFWwindow* handle = window->getHandle()) {
                    glfwSetWindowUserPointer(handle, nullptr);
                    glfwSetKeyCallback(handle, nullptr);
                    glfwSetCharCallback(handle, nullptr);
                    glfwSetScrollCallback(handle, nullptr);
                    glfwSetCursorPosCallback(handle, nullptr);
                    glfwSetMouseButtonCallback(handle, nullptr);
                    glfwSetWindowSizeCallback(handle, nullptr);
                    glfwSetDropCallback(handle, nullptr);
                }
            }

            // 清理窗口
            m_windowMap.clear();

            // 重置回调
            glfwSetErrorCallback(nullptr);
            glfwSetJoystickCallback(nullptr);

            // 终止 GLFW
            glfwTerminate();
            
            TINA_LOG_INFO("WindowManager terminated successfully");
        } catch (const std::exception& e) {
            TINA_LOG_ERROR("Error during WindowManager termination: {}", e.what());
        }
    }

    WindowHandle WindowManager::createWindow(const Window::WindowConfig& config)
    {
        WindowHandle handle;
        handle.idx = static_cast<uint16_t>(m_windowMap.size());

        auto window = std::make_unique<Window>(this, handle, config);
        if (!window->getHandle()) {
            TINA_LOG_ERROR("WindowManager::createWindow", "Failed to create GLFW window");
            return WindowHandle{UINT16_MAX};
        }

        m_windowMap[handle.idx] = std::move(window);
        setupCallbacks(handle, m_windowMap[handle.idx]->getHandle());

        Event event = createWindowEvent(Event::WindowCreate, handle, m_windowMap[handle.idx]->getHandle());
        m_context->getEventQueue().pushEvent(event);

        return handle;
    }

    void WindowManager::destroyWindow(WindowHandle handle)
    {
        auto it = m_windowMap.find(handle.idx);
        if (it != m_windowMap.end()) {
            Event event = createWindowEvent(Event::WindowDestroy, handle);
            m_context->getEventQueue().pushEvent(event);
            m_windowMap.erase(it);
        }
    }

    Window* WindowManager::getWindow(WindowHandle handle)
    {
        auto it = m_windowMap.find(handle.idx);
        return it != m_windowMap.end() ? it->second.get() : nullptr;
    }

    GLFWwindow* WindowManager::getGLFWwindow(WindowHandle handle) const
    {
        auto it = m_windowMap.find(handle.idx);
        return it != m_windowMap.end() ? it->second->getHandle() : nullptr;
    }

    WindowHandle WindowManager::findHandle(GLFWwindow* window) const
    {
        for (const auto& [idx, win] : m_windowMap) {
            if (win->getHandle() == window) {
                WindowHandle handle;
                handle.idx = idx;
                return handle;
            }
        }
        return WindowHandle{UINT16_MAX};
    }

    void WindowManager::pollEvents()
    {
        glfwPollEvents();
    }

    bool WindowManager::pollEvent(Event& event)
    {
        return m_context->getEventQueue().pollEvent(event);
    }

    void WindowManager::processMessage()
    {
        glfwPollEvents();
        
        for (const auto& [idx, window] : m_windowMap) {
            if (window->shouldClose()) {
                WindowHandle handle;
                handle.idx = idx;
                Event event = createWindowEvent(Event::WindowClose, handle);
                m_context->getEventQueue().pushEvent(event);
            }
        }
    }

    void WindowManager::setupCallbacks(WindowHandle handle, GLFWwindow* window)
    {
        glfwSetWindowUserPointer(window, this);
        
        glfwSetKeyCallback(window, [](GLFWwindow* w, int key, int scancode, int action, int mods) {
            auto* wm = static_cast<WindowManager*>(glfwGetWindowUserPointer(w));
            if (wm) {
                WindowHandle h = wm->findHandle(w);
                wm->eventCallback_key(h, w, key, scancode, action, mods);
            }
        });

        glfwSetCharCallback(window, [](GLFWwindow* w, uint32_t codepoint) {
            auto* wm = static_cast<WindowManager*>(glfwGetWindowUserPointer(w));
            if (wm) {
                WindowHandle h = wm->findHandle(w);
                wm->eventCallback_char(h, w, codepoint);
            }
        });

        glfwSetScrollCallback(window, [](GLFWwindow* w, double dx, double dy) {
            auto* wm = static_cast<WindowManager*>(glfwGetWindowUserPointer(w));
            if (wm) {
                WindowHandle h = wm->findHandle(w);
                wm->eventCallback_scroll(h, w, dx, dy);
            }
        });

        glfwSetCursorPosCallback(window, [](GLFWwindow* w, double mx, double my) {
            auto* wm = static_cast<WindowManager*>(glfwGetWindowUserPointer(w));
            if (wm) {
                WindowHandle h = wm->findHandle(w);
                wm->eventCallback_cursorPos(h, w, mx, my);
            }
        });

        glfwSetMouseButtonCallback(window, [](GLFWwindow* w, int button, int action, int mods) {
            auto* wm = static_cast<WindowManager*>(glfwGetWindowUserPointer(w));
            if (wm) {
                WindowHandle h = wm->findHandle(w);
                wm->eventCallback_mouseButton(h, w, button, action, mods);
            }
        });

        glfwSetWindowSizeCallback(window, [](GLFWwindow* w, int width, int height) {
            auto* wm = static_cast<WindowManager*>(glfwGetWindowUserPointer(w));
            if (wm) {
                WindowHandle h = wm->findHandle(w);
                wm->eventCallback_windowSize(h, w, width, height);
            }
        });

        glfwSetDropCallback(window, [](GLFWwindow* w, int count, const char** paths) {
            auto* wm = static_cast<WindowManager*>(glfwGetWindowUserPointer(w));
            if (wm) {
                WindowHandle h = wm->findHandle(w);
                wm->eventCallback_dropFile(h, w, count, paths);
            }
        });
    }

    void WindowManager::errorCallback(int error, const char* description)
    {
        TINA_LOG_ERROR("GLFW Error", "({}) {}", error, description);
    }

    void WindowManager::joystickCallback(int jid, int event)
    {
        if (WindowManager* manager = WindowManager::getInstance()) {
            manager->eventCallback_joystick(jid, event);
        }
    }

    void WindowManager::eventCallback_key(WindowHandle handle, GLFWwindow* window, int32_t key, int32_t scancode, int32_t action, int32_t mods)
    {
        Event event = createKeyEvent(handle, key, scancode, action, mods);
        m_context->getEventQueue().pushEvent(event);
    }

    void WindowManager::eventCallback_char(WindowHandle handle, GLFWwindow* window, uint32_t codepoint)
    {
        Event event(Event::Char);
        event.windowHandle = handle;
        event.character.codepoint = codepoint;
        m_context->getEventQueue().pushEvent(event);
    }

    void WindowManager::eventCallback_scroll(WindowHandle handle, GLFWwindow* window, double dx, double dy)
    {
        Event event = createMouseScrollEvent(handle, dx, dy);
        m_context->getEventQueue().pushEvent(event);
    }

    void WindowManager::eventCallback_cursorPos(WindowHandle handle, GLFWwindow* window, double mx, double my)
    {
        Event event(Event::CursorPos);
        event.windowHandle = handle;
        event.cursorPos.x = mx;
        event.cursorPos.y = my;
        m_context->getEventQueue().pushEvent(event);
    }

    void WindowManager::eventCallback_mouseButton(WindowHandle handle, GLFWwindow* window, int32_t button, int32_t action, int32_t mods)
    {
        Event event = createMouseButtonEvent(handle, button, action, mods);
        m_context->getEventQueue().pushEvent(event);
    }

    void WindowManager::eventCallback_windowSize(WindowHandle handle, GLFWwindow* window, int32_t width, int32_t height)
    {
        // 创建窗口大小改变事件
        Event event = createWindowResizeEvent(handle, width, height);
        m_context->getEventQueue().pushEvent(event);

        // 更新渲染视口
        updateBgfxViewport(handle, width, height);
    }

    void WindowManager::updateBgfxViewport(WindowHandle handle, int32_t width, int32_t height)
    {
        if (bgfx::getInternalData() && bgfx::getInternalData()->context) {
            // 设置视口大小
            bgfx::reset(width, height, BGFX_RESET_VSYNC);
            
            // 更新视口设置
            bgfx::setViewRect(0, 0, 0, uint16_t(width), uint16_t(height));
            
            // 设置视图变换矩阵（如果需要）
            bgfx::setViewTransform(0, nullptr, nullptr);
            
            TINA_LOG_DEBUG("Updated bgfx viewport: {}x{}", width, height);
        }
    }

    void WindowManager::eventCallback_dropFile(WindowHandle handle, GLFWwindow* window, int32_t count, const char** filePaths)
    {
        Event event(Event::DropFile);
        event.windowHandle = handle;
        event.dropFile.count = count;
        event.dropFile.paths = filePaths;
        m_context->getEventQueue().pushEvent(event);
    }

    void WindowManager::eventCallback_joystick(int jid, int action)
    {
        Event event(Event::Gamepad);
        event.gamepad.jid = jid;
        event.gamepad.action = action;
        m_context->getEventQueue().pushEvent(event);
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
#else
            return nullptr;
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
        return nullptr;
#else
        return nullptr;
#endif
    }

} // namespace Tina
