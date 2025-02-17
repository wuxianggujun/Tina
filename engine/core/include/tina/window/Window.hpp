//
// Created by wuxianggujun on 2025/2/5.
//

#pragma once
#include "tina/core/Core.hpp"
#include <GLFW/glfw3.h>
#include <string>

namespace Tina
{
    class WindowManager;

    struct WindowHandle
    {
        uint16_t idx;
        // Correct operator overloading syntax
        bool operator==(const WindowHandle& other) const { return idx == other.idx; }
        bool operator!=(const WindowHandle& other) const { return idx != other.idx; }
        // Less than operator remains correct
        bool operator<(const WindowHandle& other) const { return idx < other.idx; }
    };


    inline bool isValid(const WindowHandle _handle)
    {
        return UINT16_MAX != _handle.idx;
    }


    class Window
    {
    public:
        struct WindowConfig
        {
            const char* title = "Tina Engine";  // 默认标题
            int32_t width = 1280;              // 默认宽度
            int32_t height = 720;              // 默认高度
            bool vsync = true;                 // 默认开启垂直同步
            bool fullscreen = false;           // 默认窗口模式
        };

        Window(WindowManager* windowManager, WindowHandle handle, const WindowConfig& config);
        ~Window();
        [[nodiscard]] bool shouldClose() const;

        [[nodiscard]] GLFWwindow* getHandle() const { return m_handle; }
        [[nodiscard]] WindowHandle getWindowHandle() const { return m_windowHandle; }

        void setTitle(const char* title);
        void setPosition(int32_t x, int32_t y);
        void setFullscreen(bool fullscreen);

        [[nodiscard]] int32_t getWidth() const { 
            int width, height;
            glfwGetWindowSize(m_handle, &width, &height);
            return width;
        }
        
        [[nodiscard]] int32_t getHeight() const {
            int width, height;
            glfwGetWindowSize(m_handle, &width, &height);
            return height;
        }

        [[nodiscard]] const char* getTitle() const {
            return glfwGetWindowTitle(m_handle);
        }

        [[nodiscard]] bool isVSync() const {
            return glfwGetWindowAttrib(m_handle, GLFW_DOUBLEBUFFER);
        }

        [[nodiscard]] bool isFullscreen() const {
            return glfwGetWindowMonitor(m_handle) != nullptr;
        }

        // 禁止拷贝
        Window(const Window&) = delete;
        Window& operator=(const Window&) = delete;

        // 窗口基本操作
        void update();
        void close();
        void* getNativeHandle() const;

        // 设置窗口属性
        void setVSync(bool enabled);
        void setSize(uint32_t width, uint32_t height);

        // 输入状态查询
        static bool isKeyPressed(int32_t key);
        static bool isMouseButtonPressed(int32_t button);
        static void getMousePosition(double& x, double& y);
        static void setMousePosition(double x, double y);
        static void setMouseCursor(bool visible);

    private:
        WindowManager* m_windowManager;
        WindowHandle m_windowHandle;
        GLFWwindow* m_handle;
    };
} // Tina

// 为 WindowHandle 添加哈希函数支持
namespace std {
    template<>
    struct hash<Tina::WindowHandle> {
        size_t operator()(const Tina::WindowHandle& handle) const noexcept {
            return std::hash<uint16_t>{}(handle.idx);
        }
    };
}
