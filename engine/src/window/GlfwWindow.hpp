//
// Created by wuxianggujun on 2024/7/12.
//

#ifndef TINA_WINDOW_GLFWWINDOW_HPP
#define TINA_WINDOW_GLFWWINDOW_HPP

#include "Window.hpp"
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include "core/Core.hpp"

namespace Tina
{
    class GlfwWindow : public Window
    {
    protected:
        struct GlfwWindowDeleter
        {
            void operator()(GLFWwindow* window) const
            {
                glfwDestroyWindow(window);
            }
        };

    public:
        GlfwWindow();
        ~GlfwWindow() override = default;

    public:
        void create(WindowConfig config) override;
        void destroy() override;
        void pollEvents() override;
        bool shouldClose() override;

    private:
        std::unique_ptr<GLFWwindow, GlfwWindowDeleter> m_window;
    };
} // Tina

#endif //TINA_WINDOW_GLFWWINDOW_HPP
