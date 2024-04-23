#include "GlfwUtils.hpp"

namespace Tina {

//    void* _windowHandle = nullptr;
//
//    GLFWwindow* _glfwMainWindow = nullptr;
//    GLFWcursor* _glfwDefaultCursor = nullptr;
//
//    bool GlfwUtils::glfwGetWindowMonitorSize(int windowX, int windowY, int* monitorWidth, int* monitorHeight) {
//        if (monitorWidth == nullptr || monitorHeight == nullptr)
//        {
//            LOG_ERROR("GlfwWIndow Error: monitorWidth or monitorHeight equal null");
//            return false;
//        }
//
//        int monitorsLength;
//        GLFWmonitor** monitors = glfwGetMonitors(&monitorsLength);
//        if (monitors == nullptr)
//        {
//            LOG_ERROR("Failed to get monitors");
//            return false;
//        }
//
//        for (int i = 0; i < monitorsLength; ++i)
//        {
//            int monitorX, monitorY;
//            glfwGetMonitorPos(monitors[i], &monitorX, &monitorY);
//
//            GLFWvidmode* monitorVidMode = (GLFWvidmode*)glfwGetVideoMode(monitors[i]);
//            if (monitorVidMode == nullptr)
//            {
//                LOG_ERROR("Failed to get video mode for monitor {}", i);
//                continue;
//            }
//
//            *monitorWidth = monitorVidMode->width;
//            *monitorHeight = monitorVidMode->height;
//
//            if (windowX == INT_MAX || windowY == INT_MAX)
//            {
//                return true;
//            }
//
//            if ((windowX > monitorX && windowX < (monitorX + *monitorWidth)) && (windowY > monitorY && windowY < (monitorY + *monitorHeight)))
//            {
//                return true;
//            }
//        }
//        return false;
//    }
//
//
//    bool GlfwUtils::glfwGetWindowMonitorSize(GLFWwindow* window, int* monitorWidth, int* monitorHeight) {
//        if (monitorWidth == nullptr || monitorHeight == nullptr)
//        {
//            LOG_ERROR("GlfwWIndow Error: monitorWidth or monitorHeight equal null");
//            return false;
//        }
//        int monitorsLength;
//        GLFWmonitor** monitors = glfwGetMonitors(&monitorsLength);
//        if (monitors == nullptr)
//        {
//            LOG_ERROR("Failed to get monitor");
//            return false;
//        }
//
//        int windowX, windowY;
//        glfwGetWindowPos(window, &windowX, &windowY);
//
//        for (int i = 0; i < monitorsLength; ++i)
//        {
//            int monitorX, monitorY;
//            glfwGetMonitorPos(monitors[i], &monitorX, &monitorY);
//
//            GLFWvidmode* monitorVidMode = (GLFWvidmode*)glfwGetVideoMode(monitors[i]);
//
//            if (monitorVidMode == nullptr)
//            {
//                LOG_ERROR("Failed to get video mode for monitor {}", i);
//                continue;
//            }
//
//            *monitorWidth = monitorVidMode->width;
//            *monitorHeight = monitorVidMode->height;
//
//            if ((windowX > monitorX && windowX < (monitorX + *monitorWidth)) && (windowY > monitorY && windowY < (monitorY + *monitorHeight)))
//            {
//                return true;
//            }
//
//        }
//        return false;
//
//    }
//
//
//    void GlfwUtils::glfwSetWindowCenter(GLFWwindow* window) {
//        int windowX, windowY;
//        glfwGetWindowPos(window, &windowX, &windowY);
//
//        int windowWidth, windowHeight;
//        glfwGetWindowSize(window, &windowWidth, &windowHeight);
//
//        windowWidth = int(windowWidth * 0.5f);
//        windowHeight = int(windowHeight * 0.5f);
//
//
//        windowX += windowWidth;
//        windowY += windowHeight;
//
//        int monitorsLength;
//        GLFWmonitor** monitors = glfwGetMonitors(&monitorsLength);
//        if (monitors == nullptr)
//        {
//            LOG_ERROR("Failed to get monitor");
//            return;
//        }
//
//        GLFWmonitor* owner = nullptr;
//
//        int ownerX = 0, ownerY = 0, ownerWidth = 1, ownerHeight = 1;
//
//        for (int i = 0; i < monitorsLength; ++i)
//        {
//            int monitorX, monitorY;
//
//            glfwGetMonitorPos(monitors[i], &monitorX, &monitorY);
//
//            GLFWvidmode* monitorVidMode = (GLFWvidmode*)glfwGetVideoMode(monitors[i]);
//            if (monitorVidMode == nullptr)
//            {
//                LOG_ERROR("Failed to get video mode for monitor {}", i);
//                continue;
//            }
//
//            int monitorWidth = monitorVidMode->width;
//            int monitorHeight = monitorVidMode->height;
//
//            if ((windowX > monitorX && windowX < (monitorX + monitorWidth)) && (windowY > monitorY && windowY < (monitorY + monitorHeight)))
//            {
//                owner = monitors[i];
//
//
//                ownerX = monitorX;
//                ownerY = monitorY;
//
//                ownerWidth = monitorWidth;
//                ownerHeight = monitorHeight;
//            }
//        }
//
//        if (owner != nullptr)
//        {
//            glfwSetWindowPos(window, ownerX + int(ownerWidth * 0.5f) - windowWidth, ownerY + int(ownerHeight * 0.5f) - windowHeight);
//        }
//
//    }
//
//
//    GLFWwindow* GlfwUtils::glfwCreateWindowGeometry(const char* windowTitle) {
//        bool mainWindowMaximized = true;
//
//        int windowX = INT_MAX;
//        int windowY = INT_MAX;
//
//        int windowWidth = 1600;
//        int windowHeight = 900;
//
//        glfwWindowHint(GLFW_FOCUSED, GLFW_TRUE);
//        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
//        glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_FALSE);
//
//        GLFWmonitor* monitor = glfwFindWindowMonitor(windowX, windowY);
//
//        if (monitor == glfwGetPrimaryMonitor())
//        {
//            glfwWindowHint(GLFW_MAXIMIZED, mainWindowMaximized ? GLFW_TRUE : GLFW_FALSE);
//        }
//
//        float scaleX = 1.0f, scaleY = 1.0f;
//#ifdef GLFW_EXPOSE_NATIVE_WIN32
//        glfwGetMonitorContentScale(monitor, &scaleX, &scaleY);
//#endif
//        GLFWwindow* window = glfwCreateWindow(static_cast<int>(windowWidth / scaleX), static_cast<int>(windowHeight / scaleY), windowTitle, nullptr, nullptr);
//        if (window == nullptr)
//        {
//            LOG_ERROR("Window equal null!");
//            return nullptr;
//        }
//
//        bool hasPosition = true;
//
//        if (hasPosition)
//        {
//
//            if (windowX != INT_MAX && windowY != INT_MAX)
//            {
//                glfwSetWindowPos(window, windowX, windowY);
//            }
//
//            if (mainWindowMaximized)
//            {
//                glfwMaximizeWindow(window);
//            }
//
//        }
//        else
//        {
//            glfwSetWindowCenter(window);
//        }
//        return window;
//    }
//
//    void GlfwUtils::glfwSaveWindowGeometry(GLFWwindow* window) {
//        int mainWindowMaximized = glfwGetWindowAttrib(window, GLFW_MAXIMIZED);
//        mainWindowMaximized = true;
//
//        float xs = 1.0f, ys = 1.0f;
//        int windowX = 0, windowY = 0;
//        int windowWidth = 0, windowHeight = 0;
//        glfwGetWindowPos(window, &windowX, &windowY);
//        glfwGetWindowSize(window, &windowWidth, &windowHeight);
//
//    }
//
//    GLFWmonitor* GlfwUtils::glfwFindWindowMonitor(GLFWwindow* window) {
//        GLFWmonitor* monitor = glfwGetWindowMonitor(window);
//        if (monitor == nullptr)
//        {
//            int windowX = 0, windowY = 0;
//            glfwGetWindowPos(window, &windowX, &windowY);
//            return glfwFindWindowMonitor(windowX, windowY);
//        }
//    }
//
//
//    GLFWmonitor* GlfwUtils::glfwFindWindowMonitor(int windowX, int windowY) {
//        GLFWmonitor* monitor = nullptr;
//
//        int monitorCount = 0;
//
//        GLFWmonitor** monitors = glfwGetMonitors(&monitorCount);
//
//        for (int i = 0; i < monitorCount; ++i)
//        {
//            int mx, my, mw, mh;
//
//            glfwGetMonitorWorkarea(monitors[i], &mx, &my, &mw, &mh);
//
//            if (monitor == nullptr || (windowX >= mx && windowY >= my && windowX <= mx + mw && windowY <= my + mh)) {
//                monitor = monitors[i];
//            }
//        }
//        return monitor;
//    }
//
//    bool GlfwUtils::glfwIsWindowFoused(GLFWwindow* window) {
//#ifdef __EMSCRIPTEN__
//        return true;
//#else
//        return glfwGetWindowAttrib(window, GLFW_FOCUSED) != 0;
//#endif // __EMSCRIPTEN__
//    }
//
//    bool GlfwUtils::glfwIsAnyMouseButtonDown(GLFWwindow* window) {
//        for (int i = GLFW_MOUSE_BUTTON_1; i <= GLFW_MOUSE_BUTTON_LAST; ++i)
//        {
//            if (glfwGetMouseButton(window, i) == GLFW_PRESS)
//            {
//                return true;
//            }
//        }
//        return false;
//    }
//
//    int GlfwUtils::glfwTranslateUntranslatedKey(int key, int scanCode) {
//#if GLFW_HAS_GETKEYNAME && !defined(__EMSCRIPTEN__)
//        // GLFW 3.1+ attempts to "untranslated" keys, which goes the opposite of what every other framework does, making using lettered shortcuts difficult.
//        // (It had reasons to do so: namely GLFW is/was more likely to be used for WASD-type game controls rather than lettered shortcuts, but IHMO the 3.1 change could have been done differently)
//        // See https://github.com/glfw/glfw/issues/1502 for details.
//        // Adding a workaround to undo this (so our keys are translated->untranslated->translated, likely a lossy process).
//        // This won't cover edge cases but this is at least going to cover common cases.
//        if (key >= GLFW_KEY_KP_0 && key <= GLFW_KEY_KP_EQUAL)
//            return key;
//        const char* key_name = glfwGetKeyName(key, scanCode);
//        if (key_name && key_name[0] != 0 && key_name[1] == 0)
//        {
//            const char char_names[] = "`-=[]\\,;\'./";
//            const int char_keys[] = {
//                GLFW_KEY_GRAVE_ACCENT, GLFW_KEY_MINUS, GLFW_KEY_EQUAL, GLFW_KEY_LEFT_BRACKET,
//                GLFW_KEY_RIGHT_BRACKET, GLFW_KEY_BACKSLASH, GLFW_KEY_COMMA, GLFW_KEY_SEMICOLON,
//                GLFW_KEY_APOSTROPHE, GLFW_KEY_PERIOD, GLFW_KEY_SLASH, 0 };
//            //FOUNDATION_ASSERT(ARRAY_COUNT(char_names) == ARRAY_COUNT(char_keys));
//            if (key_name[0] >= '0' && key_name[0] <= '9') { key = GLFW_KEY_0 + (key_name[0] - '0'); }
//            else if (key_name[0] >= 'A' && key_name[0] <= 'Z') { key = GLFW_KEY_A + (key_name[0] - 'A'); }
//            else if (key_name[0] >= 'a' && key_name[0] <= 'z') { key = GLFW_KEY_A + (key_name[0] - 'a'); }
//            else if (const char* p = strchr(char_names, key_name[0])) { key = char_keys[p - char_names]; }
//        }
//        // if (action == GLFW_PRESS) printf("key %d scancode %d name '%s'\n", key, scancode, key_name);
//#else
//        FOUNDATION_UNUSED(scancode);
//#endif
//        return key;
//    }
//
//    int GlfwUtils::glfwKeyToModifier(int key) {
//        if (key == GLFW_KEY_LEFT_CONTROL || key == GLFW_KEY_RIGHT_CONTROL)
//        {
//            return GLFW_MOD_CONTROL;
//        }
//
//        if (key == GLFW_KEY_LEFT_SHIFT || key == GLFW_KEY_RIGHT_SHIFT)
//        {
//            return GLFW_MOD_SHIFT;
//        }
//
//        if (key == GLFW_KEY_LEFT_ALT || key == GLFW_KEY_RIGHT_ALT)
//        {
//            return GLFW_MOD_ALT;
//        }
//
//        if (key == GLFW_KEY_LEFT_SUPER || key == GLFW_KEY_RIGHT_SUPER)
//        {
//            return GLFW_MOD_SUPER;
//        }
//
//        return 0;
//    }

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
