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

    Window::Window() {
        // inits glfw
        if (!glfwInit()) {
            fail = 1;
            return;
        }
        glfwSetErrorCallback(GlfwTool::ErrorCallback);
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        window = glfwCreateWindow(750, 750, "Platformer", nullptr, nullptr);

        // Call bgfx::renderFrame before bgfx::init to signal to bgfx not to create a render thread.
        bgfx::renderFrame();
        bgfx::Init bgfxInit;

        //Platform specific config
#if BX_PLATFORM_LINUX || BX_PLATFORM_BSD
        bgfxInit.platformData.ndt = glfwGetX11Display();
    bgfxInit.platformData.nwh = (void*)(uintptr_t)glfwGetX11Window(window);
#elif BX_PLATFORM_OSX
        bgfxInit.platformData.nwh = glfwGetCocoaWindow(window);
#elif BX_PLATFORM_WINDOWS
        bgfxInit.platformData.nwh = glfwGetWin32Window(window);
#endif

        glfwGetWindowSize(window, &width, &height);


        bgfxInit.resolution.width = (uint32_t) width * 3;
        bgfxInit.resolution.height = (uint32_t) height * 3;
        bgfxInit.resolution.reset = BGFX_RESET_VSYNC;

        if (!bgfx::init(bgfxInit)) {
            fail = 1;
            return;
        }

    }
} // Tina
