#pragma once

#include "tina/core/Core.hpp"
#include "tina/window/Window.hpp"
#include "tina/event/Event.hpp"
#include <GLFW/glfw3.h>
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
    class Context;

    class TINA_CORE_API WindowManager {
    public:
        explicit WindowManager(Context* context);
        ~WindowManager();

        bool initialize();
        void terminate();

        WindowHandle createWindow(const Window::WindowConfig& config);
        void destroyWindow(WindowHandle handle);
        Window* getWindow(WindowHandle handle);

        void pollEvents();
        bool pollEvent(Event& event);
        void processMessage();

        GLFWwindow* getGLFWwindow(WindowHandle handle) const;
        WindowHandle findHandle(GLFWwindow* window) const;

        void* getNativeWindowHandle(WindowHandle handle);
        void* getNativeDisplayHandle();

        static WindowManager* getInstance() { return s_instance; }

    private:
        static void errorCallback(int error, const char* description);
        static void joystickCallback(int jid, int event);
        void setupCallbacks(WindowHandle handle, GLFWwindow* window);

        void eventCallback_key(WindowHandle handle, GLFWwindow* window, int32_t key, int32_t scancode, int32_t action, int32_t mods);
        void eventCallback_char(WindowHandle handle, GLFWwindow* window, uint32_t codepoint);
        void eventCallback_scroll(WindowHandle handle, GLFWwindow* window, double dx, double dy);
        void eventCallback_cursorPos(WindowHandle handle, GLFWwindow* window, double mx, double my);
        void eventCallback_mouseButton(WindowHandle handle, GLFWwindow* window, int32_t button, int32_t action, int32_t mods);
        void eventCallback_windowSize(WindowHandle handle, GLFWwindow* window, int32_t width, int32_t height);
        void eventCallback_dropFile(WindowHandle handle, GLFWwindow* window, int32_t count, const char** filePaths);
        void eventCallback_joystick(int jid, int action);

        void updateBgfxViewport(WindowHandle handle, int32_t width, int32_t height);

    private:
        static WindowManager* s_instance;
        Context* m_context;
        std::unordered_map<uint16_t, std::unique_ptr<Window>> m_windowMap;
    };
} // Tina
