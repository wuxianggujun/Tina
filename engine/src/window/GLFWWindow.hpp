//
// Created by wuxianggujun on 2024/7/12.
//

#ifndef TINA_WINDOW_GLFWWINDOW_HPP
#define TINA_WINDOW_GLFWWINDOW_HPP


#include <bx/bx.h>
#include <bimg/bimg.h>
#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#include "core/Platform.hpp"
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include "Window.hpp"
#include "core/Core.hpp"

namespace Tina {
    class GLFWWindow : public Window {
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

        void create(WindowConfig config) override;

        void render() override;

        void destroy() override;

        void pollEvents() override;

        bool shouldClose() override;

        [[nodiscard]] GLFWwindow *getNativeWindow() const { return m_window.get(); }

    private:
        static void errorCallback(int error, const char *description);

    private:
        Scope<GLFWwindow, GlfwWindowDeleter> m_window;
    };
} // Tina

#endif //TINA_WINDOW_GLFWWINDOW_HPP
