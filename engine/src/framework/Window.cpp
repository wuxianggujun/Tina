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

    void *Window::getNativeWindowHandle() {
#if BX_PLATFORM_LINUX || BX_PLATFORM_BSD
        nativeWindowHandle = (void*)(uintptr_t)glfwGetX11Window(window);
#elif BX_PLATFORM_OSX
        nativeWindowHandle = glfwGetCocoaWindow(window);
#elif BX_PLATFORM_WINDOWS
        nativeWindowHandle = glfwGetWin32Window(window);
#endif
        return nativeWindowHandle;
    }

    bool Window::handleResize() {
        Vector2i oldWindowSize{windowSize.width, windowSize.height};
        glfwGetWindowSize(window, &windowSize.width, &windowSize.height);

        if (windowSize.width != oldWindowSize.width || windowSize.height != oldWindowSize.height) {
            renderContext->onResize(windowSize.width, windowSize.height);
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


    Window::Window(const Configuration &config) : configuration(config) {
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
        window = glfwCreateWindow(configuration.windowWidth,
                                  configuration.windowHeight,
                                  configuration.windowTitle, nullptr, nullptr);

        // Call bgfx::renderFrame before bgfx::init to signal to bgfx not to create a render thread.
        bgfx::renderFrame();
        bgfx::Init bgfxInit;

        glfwGetWindowSize(window, &windowSize.width, &windowSize.height);

        bgfxInit.resolution.width = windowSize.width * 3;
        bgfxInit.resolution.height = windowSize.height * 3;
        bgfxInit.resolution.reset = BGFX_RESET_VSYNC;

        if (renderContext) {
            renderContext->initialize(bgfxInit, configuration.graphicsBackend, getNativeWindowHandle());
        }

        return true;
    }

    void Window::setRenderContext(Ref<RenderContext> renderContext) {
        this->renderContext = std::move(renderContext);
    }
} // Tina
