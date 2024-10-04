//
// Created by wuxianggujun on 2024/7/12.
//

#ifndef TINA_WINDOW_GLFWWINDOW_HPP
#define TINA_WINDOW_GLFWWINDOW_HPP

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include "Window.hpp"
#include "core/Core.hpp"

// build for Linux
#include "core/Platform.hpp"
#include <GLFW/glfw3native.h>


namespace Tina {
    class GLFWWindow : public Window {
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

        void create(WindowConfig config) override;

        void render() override;

        void destroy() override;

        void pollEvents() override;

        bool shouldClose() override;

        static void saveScreenShot(const std::string &fileName);

        [[nodiscard]] GLFWwindow *getNativeWindow() const { return m_window.get(); }

    private:
        static void *glfwNativeWindowHandle(GLFWwindow *window);

        static void* getNativeDisplayHandle();

        static bgfx::NativeWindowHandleType::Enum getNativeWindowHandleType();

        static void errorCallback(int error, const char *description);

    private:
        ScopePtr<GLFWwindow, GlfwWindowDeleter> m_window;
    };
} // Tina

#endif //TINA_WINDOW_GLFWWINDOW_HPP
