#include "Window.hpp"

namespace Tina {

    void Window::glfw_errorCallback(int error, const char* description)
    {
        std::cout << "Uh oh! It's a glfw error!\n" << description << std::endl;
    }

    void Window::glfw_keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
    {
        std::cout << "Key pressed: " << key << std::endl;

        if (action == GLFW_PRESS)
        {
            keyStates[key] = true;
        }
        else if (action == GLFW_RELEASE) {
            keyStates[key] = false;
        }
    }


    bool Window::handleResize()
    {
        int oldwidth = width;
        int oldHegiht = height;
        glfwGetWindowSize(window, &width, &height);
        if (width != oldwidth || height != oldHegiht)
        {
            bgfx::reset((uint32_t)width, (uint32_t)height, BGFX_RESET_VSYNC);
            return true;
        }

        return false;
    }

    bool Window::keyStates[GLFW_KEY_LAST + 1] = { false };

    Window::Window() {
        if (!glfwInit())
        {
            fail = 1;
            return;
        }

        glfwSetErrorCallback(glfw_errorCallback);
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

        window = glfwCreateWindow(750, 750, "Platformer", nullptr, nullptr);
        glfwSetKeyCallback(window, glfw_keyCallback);

        bgfx::renderFrame();

        bgfx::Init bgfxInit;

#if BX_PLATFORM_LINUX || BX_PLATFORM_BSD
        bgfxInit.platformData.ndt = glfwGetX11Display();
        bgfxInit.platformData.nwh = (void*)(uintptr_t)glfwGetX11Display(window);
#elif BX_PLATFORM_OSX
        bgfxInit.platformData.nwh = glfwGetCocoaWindow(window);
#elif BX_PLATFORM_WINDOWS
        bgfxInit.platformData.nwh = glfwGetWin32Window(window);
#endif // BX_PLATFORM_LINUX || BX_PLATFORM_BSD

        glfwGetWindowSize(window, &width, &height);

        bgfxInit.resolution.width = (uint32_t)width * 3;
        bgfxInit.resolution.height = (uint32_t)height * 3;

        bgfxInit.resolution.reset = BGFX_RESET_VSYNC;

        if (!bgfx::init(bgfxInit))
        {
            fail = 1;
            return;
        }
    }


}


