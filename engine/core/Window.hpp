#ifndef TINA_WINDOW_HPP
#define TINA_WINDOW_HPP

#include <iostream>

#include <bx/bx.h>
#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#include <GLFW/glfw3.h>
#include <bitset>

#if BX_PLATFORM_LINUX
#define GLFW_EXPOSE_NATIVE_X11
#elif BX_PLATFORM_WINDOWS
#define GLFW_EXPOSE_NATIVE_WIN32
#elif BX_PLATFORM_OSX
#define GLFW_EXPOSE_NATIVE_COCOA
#endif // BX_PLATFORM_LINUX

#include <GLFW/glfw3native.h>

namespace Tina {
    class Window {
    public:
        Window();
        bool handleResize();
        int fail;

        GLFWwindow* window;
        int width;
        int height;

        double xScale;
        double yScale;

        static bool keyStates[GLFW_KEY_LAST + 1];

    private:
        static void glfw_errorCallback(int error, const char* description);
        static void glfw_keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    };


}

#endif
