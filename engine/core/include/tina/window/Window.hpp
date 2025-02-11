//
// Created by wuxianggujun on 2025/2/5.
//

#pragma once
#include <GLFW/glfw3.h>
#include <bx/bx.h>
#include <bgfx/bgfx.h>
#include <bx/handlealloc.h>
#include <bx/thread.h>
#include <bx/mutex.h>
#include <bx/allocator.h>
#include "tina/container/String.hpp"

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
            const char* title;
            int32_t width;
            int32_t height;
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
