//
// Created by wuxianggujun on 2024/4/27.
//

#include "Window.hpp"
#include <cassert>
#include <cstdio>
#include "tool/GlfwTool.hpp"
#include "framework/log/Log.hpp"
#include <stb/stb_image.h>
#include "Core.hpp"

namespace Tina {

    bool Window::initialize(const WindowProps &props) {
        LOG_INIT("logs/log.log", Tina::LogMode::DEFAULT);

        glfwSetErrorCallback(GlfwTool::ErrorCallback);
        if (!glfwInit())
            return false;
        videoMode = *(glfwGetVideoMode(glfwGetPrimaryMonitor()));
#ifdef DEBUG
        glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
#endif
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 6);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        window = glfwCreateWindow(1080, 720,
                                  "Tina", nullptr, nullptr);

        if (!window) {
            return false;
        }

        glfwMakeContextCurrent(window);

        return true;
    }

    void Window::destroy() {
        printf("Window::destroy\n");
        glfwDestroyWindow(window);
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
        processInput(this->window);

        /* Swap front and back buffers */
        //glfwSwapBuffers(window);

        /* Poll for and process events */
        glfwPollEvents();

    }

    Window::~Window() {
        destroy();
    }


/*
    void Window::setIcon(const std::filesystem::path &iconPath) {
        int width, height, channels;
        stbi_set_flip_vertically_on_load(0);
        stbi_uc *data = stbi_load(iconPath.string().c_str(), &width, &height, &channels, 4);
        //ASSERT(data, "Failed to load image!");
        //ASSERT(channels == 4, "Icon must be RGBA");
        GLFWimage images[1];
        images[0].width = width;
        images[0].height = height;
        images[0].pixels = data;
        glfwSetWindowIcon(window, 1, images);
        stbi_image_free(data);
    }
*/

    /* void Window::setWindowMode(WindowMode mode, size_t width, size_t height) {
         if (!window)
             return;
         if (mode == windowData.windowMode)
             return;

         GLFWmonitor *monitor = nullptr;

         if (windowData.windowMode == WindowMode::WINDOWED) {
             oldWindowModeParams.width = windowData.width;
             oldWindowModeParams.height = windowData.height;
             glfwGetWindowPos(window, reinterpret_cast<int *>(&(oldWindowModeParams.xPos)),
                              reinterpret_cast<int *>(&(oldWindowModeParams.yPos)));
         }

         if (mode == WindowMode::BORDERLESS) {
             width = videoMode.width;
             height = videoMode.height;
             monitor = glfwGetPrimaryMonitor();
         } else if (mode == WindowMode::WINDOWED && (width == 0 || height == 0)) {
             width = oldWindowModeParams.width;
             height = oldWindowModeParams.height;
         } else if (mode == WindowMode::FULLSCREEN) {
             if (width == 0 || height == 0) {
                 width = videoMode.width;
                 height = videoMode.height;
             }
             monitor = glfwGetPrimaryMonitor();
         } else if (mode != WindowMode::WINDOWED) {
             if (width == 0 || height == 0) {
                 width = oldWindowModeParams.width;
                 height = oldWindowModeParams.height;
             }
             mode = WindowMode::WINDOWED;
         }

         windowData.width = width;
         windowData.height = height;

         // TODO 后面写窗口重置事件


         glfwSetWindowMonitor(window, monitor, static_cast<int>(oldWindowModeParams.xPos),
                              static_cast<int>(oldWindowModeParams.yPos), static_cast<int>(width),
                              static_cast<int>(height), videoMode.refreshRate);
     }*/

    void Window::maximizeWindow() {
        glfwMaximizeWindow(window);
    }

    void Window::restoreWindow() {
        glfwRestoreWindow(window);
    }

    void Window::setCursorPosition(double xPos, double yPos) {
        glfwSetCursorPos(window, xPos, yPos);
    }

    Window::Window(const WindowProps &props) {

    }


} // Tina
