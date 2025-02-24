#pragma once

#include "tina/core/Core.hpp"
#include "tina/window/Window.hpp"
#include "tina/delegate/Delegate.hpp"
#include "tina/core/Singleton.hpp"

#if BX_PLATFORM_WINDOWS
    #define GLFW_EXPOSE_NATIVE_WIN32
#elif BX_PLATFORM_LINUX
    #define GLFW_EXPOSE_NATIVE_X11
#elif BX_PLATFORM_OSX
    #define GLFW_EXPOSE_NATIVE_COCOA
#endif
#include <GLFW/glfw3native.h>
#include <unordered_map>
#include <memory>

namespace Tina {

    class EventManager;  // Forward declaration

    // 平台相关句柄封装
    struct NativeHandles {
        void* windowHandle;  // Native window handle (nwh)
        void* displayHandle; // Native display handle (ndt)
    };

    // 窗口事件数据结构
    struct WindowEventData {
        WindowHandle handle;
        int32_t width;
        int32_t height;
    };

    struct KeyEventData {
        WindowHandle handle;
        int32_t key;
        int32_t scancode;
        int32_t action;
        int32_t mods;
    };

    struct MouseEventData {
        WindowHandle handle;
        double x;
        double y;
        int32_t button;
        int32_t action;
        int32_t mods;
    };

    struct ScrollEventData {
        WindowHandle handle;
        double xOffset;
        double yOffset;
    };

    struct CharEventData {
        WindowHandle handle;
        uint32_t codepoint;
    };

    struct DropEventData {
        WindowHandle handle;
        int32_t count;
        const char** paths;
    };

    class TINA_CORE_API WindowManager : public Singleton<WindowManager> {
    public:
        friend class Singleton<WindowManager>;
        WindowManager();
        ~WindowManager() override;

        bool initialize();
        void terminate();

        WindowHandle createWindow(const Window::WindowConfig& config);
        void destroyWindow(WindowHandle handle);
        [[nodiscard]] Window* getWindow(WindowHandle handle) const;

        void pollEvents();
        void processMessage();

        // 事件委托
        MulticastDelegate<void(const WindowEventData&)> onWindowCreate;
        MulticastDelegate<void(const WindowEventData&)> onWindowClose;
        MulticastDelegate<void(const WindowEventData&)> onWindowResize;
        MulticastDelegate<void(const KeyEventData&)> onKeyEvent;
        MulticastDelegate<void(const MouseEventData&)> onMouseButton;
        MulticastDelegate<void(const MouseEventData&)> onMouseMove;
        MulticastDelegate<void(const ScrollEventData&)> onScroll;
        MulticastDelegate<void(const CharEventData&)> onChar;
        MulticastDelegate<void(const DropEventData&)> onDrop;

        [[nodiscard]] GLFWwindow* getGLFWwindow(WindowHandle handle) const;
        WindowHandle findHandle(GLFWwindow* window) const;

        [[nodiscard]] NativeHandles getNativeHandles(WindowHandle handle) const;
        [[nodiscard]] void* getNativeWindowHandle(WindowHandle handle) const;
        [[nodiscard]] void* getNativeDisplayHandle() const;

    private:
        static void errorCallback(int error, const char* description);
        void setupCallbacks(WindowHandle handle, GLFWwindow* window);

        void eventCallback_key(WindowHandle handle, GLFWwindow* window, int32_t key, int32_t scancode, int32_t action, int32_t mods) const;
        void eventCallback_char(WindowHandle handle, GLFWwindow* window, uint32_t codepoint) const;
        void eventCallback_scroll(WindowHandle handle, GLFWwindow* window, double dx, double dy) const;
        void eventCallback_cursorPos(WindowHandle handle, GLFWwindow* window, double mx, double my) const;
        void eventCallback_mouseButton(WindowHandle handle, GLFWwindow* window, int32_t button, int32_t action, int32_t mods) const;
        void eventCallback_windowSize(WindowHandle handle, GLFWwindow* window, int32_t width, int32_t height) const;
        void eventCallback_dropFile(WindowHandle handle, GLFWwindow* window, int32_t count, const char** filePaths) const;

        std::unordered_map<uint16_t, std::unique_ptr<Window>> m_windowMap;
        EventManager* m_eventManager;
    };
} // namespace Tina
