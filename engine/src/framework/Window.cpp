//
// Created by wuxianggujun on 2024/4/27.
//

#include "Window.hpp"
#include <glad/gl.h>
#include <GLFW/glfw3.h>

namespace Tina {

    Window::Window(const char *title, size_t width, size_t height):windowWidth(width),windowHeight(height),windowTitle(title) {
    }

    bool Window::initialize() {
        if (!glfwInit())return false;

        window = glfwCreateWindow(static_cast<int>(windowWidth),static_cast<int>(windowHeight),windowTitle, nullptr, nullptr);
        if (!window) return false;


        return true;
    }

    void Window::close() {
        if (window){
            glfwDestroyWindow(window);
        }
        glfwTerminate();
        window = nullptr;
    }

    bool Window::shouldClose() const {
        return !glfwWindowShouldClose(window);
    }

    void Window::update() {
        /* Render here */
        glClear(GL_COLOR_BUFFER_BIT);

        /* Swap front and back buffers */
        glfwSwapBuffers(window);

        /* Poll for and process events */
        glfwPollEvents();
    }


} // Tina