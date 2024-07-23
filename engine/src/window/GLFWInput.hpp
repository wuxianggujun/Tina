//
// Created by wuxianggujun on 2024/7/23.
//

#ifndef TINA_WINDOW_GLFWINPUT_HPP
#define TINA_WINDOW_GLFWINPUT_HPP

#include "InputHandler.hpp"
#include "GLFWWindow.hpp"

namespace Tina
{
    class GLFWInput final : public InputHandler
    {
    public:
        explicit GLFWInput(const GLFWWindow* window);
        void initialize() override;
        void pollEvents() override;
        void processEvent() override;

    private:
        static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
        static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
        static void cursorPosCallback(GLFWwindow* window, double xpos, double ypos);
    private:
        GLFWwindow* mWindow;
    };
} // Tina

#endif //TINA_WINDOW_GLFWINPUT_HPP
