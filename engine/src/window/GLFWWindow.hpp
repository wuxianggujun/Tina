//
// Created by wuxianggujun on 2024/7/12.
//

#pragma once

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include "IWindow.hpp"

// build for Linux
#include "core/Platform.hpp"
#include <GLFW/glfw3native.h>

#include "graphics/BgfxCallback.hpp"

namespace Tina {
    class EventHandler;
    
    struct KeyboardEvent {
        int key;
        int scancode;
        int action;
        int mods;

        KeyboardEvent(int key, int scancode, int action, int mods)
            : key(key), scancode(scancode), action(action), mods(mods) {}
    };
    
    class GLFWWindow : public IWindow {
    protected:
        struct GlfwWindowDeleter {
            void operator()(GLFWwindow *window) const {
                if (window) {
                    glfwDestroyWindow(window);
                }
            }
        };

    public:
        GLFWWindow();
        ~GLFWWindow() override = default;

        // 窗口管理
        void create(const WindowConfig& config) override;
        void destroy() override;
        void pollEvents() override;
        bool shouldClose() override;
        
        // 窗口属性
        [[nodiscard]] void* getNativeWindow() const override { return m_window.get(); }
        [[nodiscard]] Vector2i getResolution() const override { return m_windowSize; }

        // 事件处理
        void setEventHandler(ScopePtr<EventHandler>&& eventHandler) override;

        // GLFW特有功能
        static void saveScreenShot(const std::string &fileName);

    private:
        // GLFW回调
        static void keyboardCallback(GLFWwindow *window, int32_t key, int32_t scancode, int32_t action, int32_t mods);
        static void windowSizeCallBack(GLFWwindow* window, int width, int height);
        static void errorCallback(int error, const char *description);
        
        // 平台相关
        static void* glfwNativeWindowHandle(GLFWwindow *window);
        static void* getNativeDisplayHandle();
        static bgfx::NativeWindowHandleType::Enum getNativeWindowHandleType();

    private:
        Vector2i m_windowSize;
        BgfxCallback m_bgfxCallback;
        ScopePtr<EventHandler> m_eventHandle;
        ScopePtr<GLFWwindow, GlfwWindowDeleter> m_window;
    };
} // Tina
