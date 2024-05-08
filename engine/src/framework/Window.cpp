//
// Created by wuxianggujun on 2024/4/27.
//

#include "Window.hpp"
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include "tool/GlfwTool.hpp"

namespace Tina {

    Window::Window(const char *title, size_t width, size_t height):windowWidth(width),windowHeight(height),windowTitle(title) {
    }

    bool Window::initialize() {
        glfwSetErrorCallback(GlfwTool::ErrorCallback);
        if (!glfwInit())return false;

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,6);

        window = glfwCreateWindow(static_cast<int>(windowWidth),static_cast<int>(windowHeight),windowTitle, nullptr, nullptr);
        if (!window) return false;

        gladLoadGL(glfwGetProcAddress);

        /* Make the window's context current */
        glfwMakeContextCurrent(window);
        glClearColor( 0.4f, 0.3f, 0.4f, 0.0f );
        return true;
    }

    void Window::destroy() {
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