//
// Created by wuxianggujun on 2024/4/27.
//

#include "Window.hpp"
#include <glad/gl.h>
#include <GLFW/glfw3.h>

namespace Tina {

    Window::Window(size_t width, size_t height, const char *title):windowWidth(width),windowHeight(height),windowTitle(title){
    }

    void Window::initialize() {
        if (!glfwInit())return;


    }

    void Window::close() {

    }

} // Tina