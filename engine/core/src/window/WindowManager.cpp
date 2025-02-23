//
// Created by wuxianggujun on 2025/2/5.
//

#include "tina/window/WindowManager.hpp"
#include "tina/log/Log.hpp"
#include "tina/event/EventQueue.hpp"
#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#include "tina/event/EventManager.hpp"
#include "tina/window/GlfwMemoryManager.hpp"

namespace Tina
{
    WindowManager* WindowManager::s_instance = nullptr;

    WindowManager::WindowManager()
    {
        s_instance = this;
        m_eventManager = EventManager::getInstance();
    }

    WindowManager::~WindowManager()
    {
        s_instance = nullptr;
        m_eventManager = nullptr;
        terminate();
    }

    bool WindowManager::initialize()
    {
        glfwSetErrorCallback(errorCallback);

        // 禁用游戏手柄支持
        glfwInitHint(GLFW_JOYSTICK_HAT_BUTTONS, GLFW_FALSE);
        glfwInitHint(GLFW_COCOA_CHDIR_RESOURCES, GLFW_FALSE);

        // 设置GLFW内存分配器
        GLFWallocator allocator;
        allocator.allocate = GlfwMemoryManager::allocate;
        allocator.reallocate = GlfwMemoryManager::reallocate;
        allocator.deallocate = GlfwMemoryManager::deallocate;
        allocator.user = nullptr;

        glfwInitAllocator(&allocator);

        TINA_ENGINE_INFO("Setting up GLFW memory allocator");

        if (!glfwInit())
        {
            TINA_ENGINE_ERROR("Failed to initialize GLFW");
            return false;
        }
        
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        TINA_ENGINE_INFO("GLFW initialized successfully");
        
        TINA_ENGINE_DEBUG("GLFW memory stats - Current: {}MB, Peak: {}MB",
            GlfwMemoryManager::getCurrentAllocated() / (1024*1024),
            GlfwMemoryManager::getPeakAllocated() / (1024*1024));

        return true;
    }

    void WindowManager::terminate()
    {
        TINA_ENGINE_INFO("Terminating WindowManager");

        try {
            if (!glfwInit()) {
                TINA_ENGINE_DEBUG("GLFW not initialized, skipping cleanup");
                return;
            }

            // 清理所有窗口
            m_windowMap.clear();

            glfwTerminate();
            TINA_ENGINE_INFO("WindowManager terminated successfully");
        } catch (const std::exception& e) {
            TINA_ENGINE_ERROR("Error during WindowManager termination: {}", e.what());
        }
    }

    WindowHandle WindowManager::createWindow(const Window::WindowConfig& config)
    {
        WindowHandle handle{};
        handle.idx = static_cast<uint16_t>(m_windowMap.size());

        auto window = std::make_unique<Window>(this, handle);
        if (!window->create(config)) {
            TINA_ENGINE_ERROR("Failed to create window");
            return WindowHandle{UINT16_MAX};
        }

        m_windowMap[handle.idx] = std::move(window);
        setupCallbacks(handle, m_windowMap[handle.idx]->getHandle());

        // 触发窗口创建事件
        const WindowEventData eventData{handle, config.width, config.height};
        onWindowCreate.invoke(eventData);

        return handle;
    }

    void WindowManager::destroyWindow(WindowHandle handle)
    {
        auto it = m_windowMap.find(handle.idx);
        if (it != m_windowMap.end()) {
            WindowEventData eventData{handle, 0, 0};
            onWindowClose.invoke(eventData);
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

    void WindowManager::processMessage()
    {
        glfwPollEvents();
        
        if (m_eventManager) {
            m_eventManager->update();
            
            for (const auto& [idx, window] : m_windowMap) {
                if (window->shouldClose()) {
                    WindowHandle handle{};
                    handle.idx = idx;
                    auto event = MakeShared<Event>(Event::WindowClose);
                    event->windowHandle = handle;
                    m_eventManager->triggerEvent(event);
                }
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
        TINA_ENGINE_ERROR("GLFW Error ({}): {}", error, description);
    }

    void WindowManager::eventCallback_key(WindowHandle handle, GLFWwindow* window, int32_t key, int32_t scancode, int32_t action, int32_t mods)
    {
        if (m_eventManager) {
            auto event = MakeShared<Event>(Event::Key);
            event->windowHandle = handle;
            event->key.key = static_cast<Event::KeyCode>(key);
            event->key.scancode = scancode;
            event->key.action = action;
            event->key.mods = static_cast<Event::KeyModifier>(mods);
            m_eventManager->triggerEvent(event);
        }
    }

    void WindowManager::eventCallback_char(WindowHandle handle, GLFWwindow* window, uint32_t codepoint)
    {
        if (m_eventManager) {
            auto event = MakeShared<Event>(Event::Char);
            event->windowHandle = handle;
            event->character.codepoint = codepoint;
            m_eventManager->triggerEvent(event);
        }
    }

    void WindowManager::eventCallback_scroll(WindowHandle handle, GLFWwindow* window, double dx, double dy)
    {
        if (m_eventManager) {
            auto event = MakeShared<Event>(Event::MouseScroll);
            event->windowHandle = handle;
            event->mouseScroll.xoffset = dx;
            event->mouseScroll.yoffset = dy;
            m_eventManager->triggerEvent(event);
        }
    }

    void WindowManager::eventCallback_cursorPos(WindowHandle handle, GLFWwindow* window, double mx, double my)
    {
        if (m_eventManager) {
            auto event = MakeShared<Event>(Event::MouseMove);
            event->windowHandle = handle;
            event->mousePos.x = mx;
            event->mousePos.y = my;
            m_eventManager->triggerEvent(event);
        }
    }

    void WindowManager::eventCallback_mouseButton(WindowHandle handle, GLFWwindow* window, int32_t button, int32_t action, int32_t mods)
    {
        if (m_eventManager) {
            double x, y;
            glfwGetCursorPos(window, &x, &y);
            
            auto event = MakeShared<Event>(Event::MouseButtonEvent);
            event->windowHandle = handle;
            event->mouseButton.button = static_cast<Event::MouseButton>(button);
            event->mouseButton.action = action;
            event->mouseButton.mods = static_cast<Event::KeyModifier>(mods);
            event->mouseButton.x = x;
            event->mouseButton.y = y;
            m_eventManager->triggerEvent(event);
        }
    }

    void WindowManager::eventCallback_windowSize(WindowHandle handle, GLFWwindow* window, int32_t width, int32_t height)
    {
        if (m_eventManager) {
            auto event = MakeShared<Event>(Event::WindowResize);
            event->windowHandle = handle;
            event->windowResize.width = width;
            event->windowResize.height = height;
            m_eventManager->triggerEvent(event);
        }
    }

    void WindowManager::eventCallback_dropFile(WindowHandle handle, GLFWwindow* window, int32_t count, const char** filePaths)
    {
        if (m_eventManager) {
            auto event = MakeShared<Event>(Event::DropFile);
            event->windowHandle = handle;
            event->dropFile.count = count;
            event->dropFile.paths = filePaths;
            m_eventManager->triggerEvent(event);
        }
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
