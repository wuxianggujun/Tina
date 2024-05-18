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

    void Window::update() {

        glfwPollEvents();

        //bgfx::frame();
    }

    void Window::destroy() {
        glfwDestroyWindow(window);
        bgfx::shutdown();
        glfwTerminate();
    }

    bool Window::isRunning() const {
        return !glfwWindowShouldClose(window);
    }

    bool Window::initialize() {
        // inits glfw
        if (!glfwInit()) {
            fail = 1;
            return false;
        }
        glfwSetErrorCallback(GlfwTool::ErrorCallback);
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        window = glfwCreateWindow(750, 750, "Platformer", nullptr, nullptr);

        // Call bgfx::renderFrame before bgfx::init to signal to bgfx not to create a render thread.
        bgfx::renderFrame();
        bgfx::Init bgfxInit;

        GlfwTool::getNativeWindow(bgfxInit);

        glfwGetWindowSize(window, &width, &height);


        bgfxInit.resolution.width = (uint32_t) width * 3;
        bgfxInit.resolution.height = (uint32_t) height * 3;
        bgfxInit.resolution.reset = BGFX_RESET_VSYNC;

        if (!bgfx::init(bgfxInit)) {
            fail = 1;
            return false;
        }
        return true;
    }
} // Tina
