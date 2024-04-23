#ifndef TINA_FRAMEWORK_GLFWUTILS_HPP
#define TINA_FRAMEWORK_GLFWUTILS_HPP

#include "log/Log.hpp"
#include "NativeWindow.hpp"

#include <cstdint>
#include <bx/thread.h>
#include <bx/spscqueue.h>
#include <bx/allocator.h>
#include <bgfx/bgfx.h>
#include <bgfx/platform.h>

namespace Tina {

    class GlfwUtils {
    public:
    
        static void glfwLogError(int error, const char* description);

        static void* getNativeWindowHandle(GLFWwindow* window);
    };

}




#endif
