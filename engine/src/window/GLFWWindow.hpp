//
// Created by wuxianggujun on 2024/7/12.
//

#ifndef TINA_WINDOW_GLFWWINDOW_HPP
#define TINA_WINDOW_GLFWWINDOW_HPP

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include "IWindow.hpp"
#include "Window.hpp"


// build for Linux
#include "core/Platform.hpp"
#include <GLFW/glfw3native.h>

namespace Tina {

    class EventHandler;
    
    struct KeyboardEvent {
        int key;
        int scancode;
        int action;
        int mods;
    };
    
    class GLFWWindow : public IWindow {
    protected:
        struct GlfwWindowDeleter {
            void operator()(GLFWwindow *window) const {
                if (window)
                {
                    glfwDestroyWindow(window);
                }
            }
        };

    public:
        GLFWWindow();

        ~GLFWWindow() override = default;

        void create(const WindowConfig& config) override;
        
        void setEventHandler(ScopePtr<EventHandler> &&eventHandler) override;
   
        void destroy() override;

        void pollEvents() override;

        bool shouldClose() override;

        static void saveScreenShot(const std::string &fileName);

        [[nodiscard]] void*getNativeWindow() const override { return m_window.get(); }

    private:

        static void keyboardCallback(GLFWwindow *window,int32_t _key, int32_t _scancode, int32_t _action, int32_t _mods);
        static void *glfwNativeWindowHandle(GLFWwindow *window);

        static void* getNativeDisplayHandle();

        static bgfx::NativeWindowHandleType::Enum getNativeWindowHandleType();

        static void errorCallback(int error, const char *description);

        BgfxCallback m_bgfxCallback;
        ScopePtr<EventHandler> m_eventHandle;
        ScopePtr<GLFWwindow, GlfwWindowDeleter> m_window;
    };
} // Tina

#endif //TINA_WINDOW_GLFWWINDOW_HPP
