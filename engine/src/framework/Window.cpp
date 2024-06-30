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

        handleResize();
    }

    bool Window::handleResize() {
        Vector2i oldWindowSize{windowSize.width, windowSize.height};
        glfwGetWindowSize(window, &windowSize.width, &windowSize.height);

        if (windowSize.width != oldWindowSize.width || windowSize.height != oldWindowSize.height) {
            bgfx::reset(windowSize.width, windowSize.height, BGFX_RESET_VSYNC);
            bgfx::setViewRect(kClearView, 0, 0, bgfx::BackbufferRatio::Equal);
            return true;
        }
        return false;
    }

    void Window::destroy() {
        glfwDestroyWindow(window);
        bgfx::shutdown();
        glfwTerminate();
    }

    bool Window::isRunning() const {
        return !glfwWindowShouldClose(window);
    }


    Window::Window(Configuration &config) : configuration(config) {
    }

    bool Window::initialize() {
#ifndef _WIN32
        glfwInitHint(GLFW_PLATFORM,GLFW_PLATFORM_X11);
#endif
        // inits glfw
        if (!glfwInit()) {
            fail = 1;
            return false;
        }
        glfwSetErrorCallback(GlfwTool::ErrorCallback);
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        window = glfwCreateWindow(static_cast<int>(configuration.windowWidth),
                                  static_cast<int>(configuration.windowHeight),
                                  configuration.windowTitle, nullptr, nullptr);

        // Call bgfx::renderFrame before bgfx::init to signal to bgfx not to create a render thread.
        bgfx::renderFrame();
        bgfx::Init bgfxInit;

#if BX_PLATFORM_LINUX || BX_PLATFORM_BSD
        bgfxInit.platformData.ndt = glfwGetX11Display();
         bgfxInit.platformData.nwh = (void*)(uintptr_t)glfwGetX11Window(window);
#elif BX_PLATFORM_OSX
        bgfxInit.platformData.nwh = glfwGetCocoaWindow(window);
#elif BX_PLATFORM_WINDOWS
        bgfxInit.platformData.nwh = glfwGetWin32Window(window);
#endif

        glfwGetWindowSize(window, &windowSize.width, &windowSize.height);

        bgfxInit.resolution.width = (uint32_t) windowSize.width * 3;
        bgfxInit.resolution.height = (uint32_t) windowSize.height * 3;
        bgfxInit.resolution.reset = BGFX_RESET_VSYNC;

        if (!bgfx::init(bgfxInit)) {
            fail = 1;
            return false;
        }
        /*// Set view 0 to the same dimensions as the window and to clear the color buffer.
        bgfx::setViewClear(kClearView, BGFX_CLEAR_COLOR);
        bgfx::setViewRect(kClearView, 0, 0, bgfx::BackbufferRatio::Equal);

        bgfx::setDebug(BGFX_DEBUG_NONE);*/

        return true;
    }
} // Tina
