//
// Created by wuxianggujun on 2024/7/23.
//

#include "GLFWInput.hpp"
#include <GLFW/glfw3.h>


namespace Tina
{
    GLFWInput::GLFWInput(const GLFWWindow* window): mWindow(window->getNativeWindow())
    {
    }

    void GLFWInput::initialize()
    {
        if (mWindow)
        {
            // Set GLFW callbacks here
            // glfwSetKeyCallback(mWindow, keyCallback);
            // glfwSetMouseButtonCallback(mWindow, mouseButtonCallback);
            // glfwSetCursorPosCallback(mWindow, cursorPosCallback);
        }
    }

    void GLFWInput::pollEvents()
    {
        glfwPollEvents();
    }

    void GLFWInput::processEvent()
    {
    }

    void GLFWInput::keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
    {
        // Handle GLFW key events here
        printf("GLFW Key event: key=%d, action=%d, mods=%d\n", key, action, mods);
    }

    void GLFWInput::mouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
    {
        // Handle GLFW mouse button events here
        printf("GLFW Mouse button event: button=%d, action=%d, mods=%d\n", button, action, mods);
    }

    void GLFWInput::cursorPosCallback(GLFWwindow* window, double xpos, double ypos)
    {
        // Handle GLFW cursor position events here
        printf("GLFW Cursor position event: xpos=%f, ypos=%f\n", xpos, ypos);
    }
} // Tina
