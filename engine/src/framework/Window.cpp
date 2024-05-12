//
// Created by wuxianggujun on 2024/4/27.
//

#include "Window.hpp"
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <cassert>
#include <cstdio>
#include "tool/GlfwTool.hpp"
//#include "glad/gl.h"
#include "framework/log/Log.hpp"

namespace Tina {

    Window::Window(const char *title, size_t width, size_t height) : windowWidth(width), windowHeight(height),
                                                                     windowTitle(title) {
    }

    bool Window::initialize() {
        LOG_INIT("logs/log.log", Tina::LogMode::DEFAULT);
        LOG_TRACE("Hello");

        glfwSetErrorCallback(GlfwTool::ErrorCallback);
        if (!glfwInit())return false;

        glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);
        glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

        window = glfwCreateWindow(static_cast<int>(windowWidth), static_cast<int>(windowHeight), windowTitle, nullptr,
                                  nullptr);
        if (!window) return false;

        /* Make the window's context current */
        glfwMakeContextCurrent(window);
        glfwSetWindowUserPointer(window, this);
//        gladLoadGL(glfwGetProcAddress);
        glfwSwapInterval(1);
        return true;
    }

    void Window::destroy() {
        printf("Window::destroy\n");
        glfwMakeContextCurrent(nullptr);
        if (this->window) {
            glfwDestroyWindow(this->window);
        }
        glfwTerminate();
    }

    bool Window::shouldClose() const {
        return !glfwWindowShouldClose(window);
    }

    void processInput(GLFWwindow *glfWwindow) {
        if (glfwGetKey(glfWwindow, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(glfWwindow, true);
            glfwDestroyWindow(glfWwindow);
        }
    }

    void Window::update() {
        //processInput(this->window);
        glfwMakeContextCurrent(this->window);

        if (glfwGetKey(this->window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(this->window, true);
            //glfwDestroyWindow(this->window);
        }
        //printf("Window::update\n");
        /* Render here */
//        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
//        glClearColor(49.f / 255, 77.f / 255, 121.f / 255, 1.f);
        /* Swap front and back buffers */
        glfwSwapBuffers(window);

        /* Poll for and process events */
        glfwPollEvents();
    }

    Window::~Window() {
        this->window = nullptr;
    }


} // Tina
