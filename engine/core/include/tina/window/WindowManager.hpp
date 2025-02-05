//
// Created by wuxianggujun on 2025/2/5.
//

#pragma once
#include <map>
#include "Window.hpp"
#if BX_PLATFORM_WINDOWS
    #define GLFW_EXPOSE_NATIVE_WIN32
#elif BX_PLATFORM_LINUX
    #define GLFW_EXPOSE_NATIVE_X11
    #define GLFW_EXPOSE_NATIVE_WAYLAND
#elif BX_PLATFORM_OSX
    #define GLFW_EXPOSE_NATIVE_COCOA
#endif
#include <GLFW/glfw3native.h>
// 最大窗口数量
#define TINA_CONFIG_MAX_WINDOWS 10

namespace Tina
{
    class Context;
    class EventQueue;

    class WindowManager
    {

    public:
        explicit WindowManager(Context* context);
        ~WindowManager();

        bool initialize();
        void terminate();

        WindowHandle createWindow(int32_t x, int32_t y, int32_t width, int32_t height, uint32_t flags,
                                  const char* title);
        void destroyWindow(WindowHandle handle);

        Window* getWindow(WindowHandle handle);

        // 处理消息队列和GLFW事件
        void processMessage();

        void* getNativeWindowHandle(WindowHandle _handle);
        void* getNativeDisplayHandle();
        bgfx::NativeWindowHandleType::Enum getNativeWindowHandleType();

        static WindowManager* getInstance() { return s_instance; }

    private:
        static WindowManager* s_instance;
        Context* m_context;
        std::map<WindowHandle, Window*> m_windowMap;
        bx::HandleAllocT<TINA_CONFIG_MAX_WINDOWS> m_windowHandleAlloc;

        WindowHandle findHandle(GLFWwindow* glfwWindow) const;

        // --- GLFW Error Callback ---
        static void errorCb(int _error, const char* _description);

        friend void keyCallback(GLFWwindow* glfwWindow, int32_t key, int32_t scancode, int32_t action, int32_t mods);
        friend void charCallback(GLFWwindow* glfwWindow, uint32_t scancode);
        friend void scrollCallback(GLFWwindow* glfwWindow, double dx, double dy);
        friend void cursorPosCallback(GLFWwindow* glfwWindow, double mx, double my);
        friend void mouseButtonCallback(GLFWwindow* glfwWindow, int32_t button, int32_t action, int32_t mods);
        friend void windowSizeCallback(GLFWwindow* glfwWindow, int32_t width, int32_t height);
        friend void dropFileCallback(GLFWwindow* glfwWindow, int32_t count, const char** filePaths);
        friend void joystickCallback(int jid, int action);

    public:
        // WindowManager 实例方法 - 处理事件并发送到 EventQueue
        void eventCallback_key(WindowHandle handle, GLFWwindow* glfwWindow, int32_t key, int32_t scancode,
                               int32_t action, int32_t mods);
        void eventCallback_char(WindowHandle handle, GLFWwindow* glfwWindow, uint32_t scancode);
        void eventCallback_scroll(WindowHandle handle, GLFWwindow* glfwWindow, double dx, double dy);
        void eventCallback_cursorPos(WindowHandle handle, GLFWwindow* glfwWindow, double mx, double my);
        void eventCallback_mouseButton(WindowHandle handle, GLFWwindow* glfwWindow, int32_t button, int32_t action,
                                       int32_t mods);
        void eventCallback_windowSize(WindowHandle handle, GLFWwindow* glfwWindow, int32_t width, int32_t height);
        void eventCallback_dropFile(WindowHandle handle, GLFWwindow* glfwWindow, int32_t count, const char** filePaths);
        void eventCallback_joystick(int jid, int action);
    };
} // Tina
