#include "GlfwUtils.hpp"

namespace Tina {

    void GlfwUtils::glfwLogError(int error, const char* description) {
        LOG_ERROR("Glfw Error: {}", description);
    }

    void* GlfwUtils::getNativeWindowHandle(GLFWwindow* window)
    {
        void* windowHandle = nullptr;
#if BX_PLATFORM_LINUX
        windowHandle =(uintptr_t)glfwGetX11Window(window);
#elif BX_PLATFORM_OSX
        windowHandle = glfwGetCocoaWindow(window);
#elif BX_PLATFORM_WINDOWS
        return glfwGetWin32Window(window);
#endif
        return windowHandle;
    }
}
