#include "log/Log.hpp"
#include "GlfwWindow.hpp"

#include <bgfx/bgfx.h>
#include <bx/bx.h>
#include <bx/allocator.h>
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

namespace Tina {

    void* _windowHandle = nullptr;

    GLFWwindow* _glfwMainWindow = nullptr;
    GLFWcursor* _glfwDefaultCursor = nullptr;

    bool GlfwWindow::glfwGetWindowMonitorSize(int windowX, int windowY, int* monitorWidth, int* monitorHeight) {
        if (monitorWidth == nullptr || monitorHeight == nullptr)
        {
            LOG_ERROR("GlfwWIndow Error: monitorWidth or monitorHeight equal null");
            return false;
        }

        int monitorsLength;
        GLFWmonitor** monitors = glfwGetMonitors(&monitorsLength);
        if (monitors == nullptr)
        {
            LOG_ERROR("Failed to get monitors");
            return false;
        }

        for (int i=0;i<monitorsLength;++i)
        {
            int monitorX, monitorY;
            glfwGetMonitorPos(monitors[i], &monitorX, &monitorY);

            GLFWvidmode* monitorVidMode = (GLFWvidmode*)glfwGetVideoMode(monitors[i]);
            if (monitorVidMode == nullptr)
            {
                LOG_ERROR("Failed to get video mode for monitor {}",i); 
                continue;
            }

            *monitorWidth = monitorVidMode->width;
            *monitorHeight = monitorVidMode->height;

            if (windowX == INT_MAX || windowY == INT_MAX)
            {
                return true;
            }

            if ((windowX > monitorX && windowX < (monitorX + *monitorWidth)) && (windowY > monitorY && windowY < (monitorY + *monitorHeight)))
            {
                return true;
            }
        }
        return false;
    }


    bool GlfwWindow::glfwGetWindowMonitorSize(GLFWwindow* window, int* monitorWidth, int* monitorHeight) {
        if (monitorWidth == nullptr || monitorHeight == nullptr)
        {
            LOG_ERROR("GlfwWIndow Error: monitorWidth or monitorHeight equal null");
            return false;
        }
        int monitorsLength;
        GLFWmonitor** monitors = glfwGetMonitors(&monitorsLength);
        if (monitors == nullptr)
        {
            LOG_ERROR("Failed to get monitor");
            return false;
        }

        int windowX, windowY;
        glfwGetWindowPos(window,&windowX,&windowY);

        for (int i=0;i<monitorsLength;++i)
        {
            int monitorX, monitorY;
            glfwGetMonitorPos(monitors[i], &monitorX, &monitorY);

            GLFWvidmode* monitorVidMode = (GLFWvidmode*)glfwGetVideoMode(monitors[i]);

            if (monitorVidMode == nullptr)
            {
                LOG_ERROR("Failed to get video mode for monitor {}", i);
                continue;
            }

            *monitorWidth = monitorVidMode->width;
            *monitorHeight = monitorVidMode->height;

            if ((windowX > monitorX && windowX < (monitorX + *monitorWidth))&& (windowY > monitorY && windowY < (monitorY + *monitorHeight)))
            {
                return true;
            }

        }
        return false;

    }


    void GlfwWindow::glfwSetWindowCenter(GLFWwindow* window) {
        int windowX, windowY;
        glfwGetWindowPos(window, &windowX, &windowY);

        int windowWidth, windowHeight;
        glfwGetWindowSize(window,&windowWidth,&windowHeight);

        windowWidth = int(windowWidth * 0.5f);
        windowHeight = int(windowHeight * 0.5f);


        windowX += windowWidth;
        windowY += windowHeight;

        int monitorsLength;
        GLFWmonitor** monitors = glfwGetMonitors(&monitorsLength);
        if (monitors == nullptr)
        {
            LOG_ERROR("Failed to get monitor");
            return;
        }

        GLFWmonitor* owner = nullptr;

        int ownerX = 0, ownerY = 0, ownerWidth = 1, ownerHeight = 1;

        for (int i=0;i<monitorsLength;++i)
        {
            int monitorX, monitorY;

            glfwGetMonitorPos(monitors[i], &monitorX, &monitorY);

            GLFWvidmode* monitorVidMode = (GLFWvidmode*)glfwGetVideoMode(monitors[i]);
            if (monitorVidMode == nullptr)
            {
                LOG_ERROR("Failed to get video mode for monitor {}", i);
                continue;
            }

            int monitorWidth = monitorVidMode->width;
            int monitorHeight = monitorVidMode->height;

            if ((windowX > monitorX && windowX < (monitorX + monitorWidth)) && (windowY > monitorY && windowY < (monitorY + monitorHeight)))
            {
                owner = monitors[i];


                ownerX = monitorX;
                ownerY = monitorY;

                ownerWidth = monitorWidth;
                ownerHeight = monitorHeight;
            }
        }

        if (owner != nullptr)
        {
            glfwSetWindowPos(window, ownerX + int(ownerWidth * 0.5f) - windowWidth,ownerY + int(ownerHeight * 0.5f) - windowHeight);
        }

    }


    GLFWwindow* GlfwWindow::glfwCreateWindowGeometry(const char* windowTitle) {
        bool mainWindowMaximized = true;

        int windowX = INT_MAX;
        int windowY = INT_MAX;

        int windowWidth = 1600;
        int windowHeight = 900;

        glfwWindowHint(GLFW_FOCUSED,GLFW_TRUE);
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_FOCUS_ON_SHOW,GLFW_FALSE);

        GLFWmonitor* monitor = glfwFindWindowMonitor(windowX, windowY);

        if (monitor == glfwGetPrimaryMonitor())
        {
            glfwWindowHint(GLFW_MAXIMIZED,mainWindowMaximized ? GLFW_TRUE : GLFW_FALSE);
        }

        float scaleX = 1.0f, scaleY = 1.0f;
#ifdef GLFW_EXPOSE_NATIVE_WIN32
        glfwGetMonitorContentScale(monitor,&scaleX,&scaleY);
#endif
        GLFWwindow* window = glfwCreateWindow(static_cast<int>(windowWidth / scaleX),static_cast<int>(windowHeight / scaleY),windowTitle,nullptr,nullptr);
        if (window == nullptr)
        {
            LOG_ERROR("Window equal null!");
            return nullptr;
        }

        bool hasPosition = true;

        if (hasPosition)
        {

            if (windowX != INT_MAX && windowY != INT_MAX)
            {
                glfwSetWindowPos(window,windowX,windowY);
            }

            if (mainWindowMaximized)
            {
                glfwMaximizeWindow(window);
            }

        }
        else
        {
            glfwSetWindowCenter(window);
        }
        return window;
    }

    void GlfwWindow::glfwSaveWindowGeometry(GLFWwindow* window) {
        int mainWindowMaximized = glfwGetWindowAttrib(window, GLFW_MAXIMIZED);
        mainWindowMaximized = true;

        float xs = 1.0f, ys = 1.0f;
        int windowX = 0, windowY = 0;
        int windowWidth = 0, windowHeight = 0;
        glfwGetWindowPos(window, &windowX, &windowY);
        glfwGetWindowSize(window, &windowWidth, &windowHeight);

    }

    GLFWmonitor* GlfwWindow::glfwFindWindowMonitor(GLFWwindow* window) {
        GLFWmonitor* monitor = glfwGetWindowMonitor(window);
        if (monitor == nullptr)
        {
            int windowX = 0, windowY = 0;
            glfwGetWindowPos(window,&windowX,&windowY);
            return glfwFindWindowMonitor(windowX, windowY);
        }
    }


    GLFWmonitor* GlfwWindow::glfwFindWindowMonitor(int windowX, int windowY) {
        GLFWmonitor* monitor = nullptr;

        int monitorCount = 0;

        GLFWmonitor** monitors = glfwGetMonitors(&monitorCount);

        for (int i = 0;i< monitorCount;++i)
        {
            int mx, my, mw, mh;

            glfwGetMonitorWorkarea(monitors[i],&mx,&my,&mw,&mh);

            if (monitor == nullptr || (windowX >= mx && windowY >= my && windowX <= mx + mw && windowY <= my + mh)) {
                monitor = monitors[i];
            }   
        }
        return monitor;
    }

    bool GlfwWindow::glfwIsWindowFoused(GLFWwindow* window) {
#ifdef __EMSCRIPTEN__
        return true;
#else
        return glfwGetWindowAttrib(window, GLFW_FOCUSED) != 0;
#endif // __EMSCRIPTEN__
    }

    bool GlfwWindow::glfwIsAnyMouseButtonDown(GLFWwindow* window) {
        for (int i = GLFW_MOUSE_BUTTON_1; i <= GLFW_MOUSE_BUTTON_LAST;++i)
        {
            if (glfwGetMouseButton(window,i) == GLFW_PRESS)
            {
                return true;
            }
        }
        return false;
    }

    int GlfwWindow::glfwTranslateUntranslatedKey(int key, int scanCode) {
#if GLFW_HAS_GETKEYNAME && !defined(__EMSCRIPTEN__)
        // GLFW 3.1+ attempts to "untranslated" keys, which goes the opposite of what every other framework does, making using lettered shortcuts difficult.
        // (It had reasons to do so: namely GLFW is/was more likely to be used for WASD-type game controls rather than lettered shortcuts, but IHMO the 3.1 change could have been done differently)
        // See https://github.com/glfw/glfw/issues/1502 for details.
        // Adding a workaround to undo this (so our keys are translated->untranslated->translated, likely a lossy process).
        // This won't cover edge cases but this is at least going to cover common cases.
        if (key >= GLFW_KEY_KP_0 && key <= GLFW_KEY_KP_EQUAL)
            return key;
        const char* key_name = glfwGetKeyName(key, scanCode);
        if (key_name && key_name[0] != 0 && key_name[1] == 0)
        {
            const char char_names[] = "`-=[]\\,;\'./";
            const int char_keys[] = {
                GLFW_KEY_GRAVE_ACCENT, GLFW_KEY_MINUS, GLFW_KEY_EQUAL, GLFW_KEY_LEFT_BRACKET,
                GLFW_KEY_RIGHT_BRACKET, GLFW_KEY_BACKSLASH, GLFW_KEY_COMMA, GLFW_KEY_SEMICOLON,
                GLFW_KEY_APOSTROPHE, GLFW_KEY_PERIOD, GLFW_KEY_SLASH, 0 };
            //FOUNDATION_ASSERT(ARRAY_COUNT(char_names) == ARRAY_COUNT(char_keys));
            if (key_name[0] >= '0' && key_name[0] <= '9') { key = GLFW_KEY_0 + (key_name[0] - '0'); }
            else if (key_name[0] >= 'A' && key_name[0] <= 'Z') { key = GLFW_KEY_A + (key_name[0] - 'A'); }
            else if (key_name[0] >= 'a' && key_name[0] <= 'z') { key = GLFW_KEY_A + (key_name[0] - 'a'); }
            else if (const char* p = strchr(char_names, key_name[0])) { key = char_keys[p - char_names]; }
        }
        // if (action == GLFW_PRESS) printf("key %d scancode %d name '%s'\n", key, scancode, key_name);
#else
        FOUNDATION_UNUSED(scancode);
#endif
        return key;
    }

    int GlfwWindow::glfwKeyToModifier(int key) {
        if (key == GLFW_KEY_LEFT_CONTROL || key == GLFW_KEY_RIGHT_CONTROL)
        {
            return GLFW_MOD_CONTROL;
        }

        if (key == GLFW_KEY_LEFT_SHIFT || key == GLFW_KEY_RIGHT_SHIFT)
        {
            return GLFW_MOD_SHIFT;
        }

        if (key == GLFW_KEY_LEFT_ALT || key == GLFW_KEY_RIGHT_ALT)
        {
            return GLFW_MOD_ALT;
        }

        if (key == GLFW_KEY_LEFT_SUPER || key == GLFW_KEY_RIGHT_SUPER)
        {
            return GLFW_MOD_SUPER;
        }

        return 0;
    }

    void GlfwWindow::glfwLogError(int error, const char* description) {
        LOG_ERROR("Glfw Error: {}",description);
    }
#if GLFW_EXPOSE_NATIVE_COCOA
#define  GLFW_EXPOSE_NATIVE_COCOA
    void* glfwPlatformWindowHandle(GLFWwindow* window) {
        return (void*)glfwGetCocoaWindow(window);
    }
#endif

   /* void GlfwWindow::glfwSetWindowMainIcon(GLFWwindow* window) {
#ifdef GLFW_EXPOSE_NATIVE_WIN32
        HWND window_handle = glfwGetWin32Window(window);
        HINSTANCE module_handle = ::GetModuleHandle(nullptr);
        HANDLE big_icon = LoadImageA(module_handle, MAKEINTRESOURCEA(GLFW_ICON), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR);
        HANDLE small_icon = LoadImageA(module_handle, MAKEINTRESOURCEA(GLFW_ICON), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
        if (big_icon)
            SendMessage(window_handle, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(big_icon));
        if (small_icon)
            SendMessage(window_handle, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(small_icon));
#endif
    }*/

#ifndef GLFW_EXPOSE_NATIVE_COCOA

    void* glfw_platform_window_handle(GLFWwindow* window)
    {
        void* window_handle = nullptr;
#if FOUNDATION_PLATFORM_LINUX
        window_handle = (void*)(uintptr_t)glfwGetX11Window(window);
#elif FOUNDATION_PLATFORM_MACOS
        // See glfw_osx.cpp
#elif FOUNDATION_PLATFORM_WINDOWS
        window_handle = glfwGetWin32Window(window);
#endif
        return window_handle;
    }
#endif

    void GlfwWindow::shutdown() {
        if (_glfwDefaultCursor)
        {
            glfwDestroyCursor(_glfwDefaultCursor);
        }

        glfwDestroyWindow(_glfwMainWindow);
        glfwTerminate();

        _glfwMainWindow = nullptr;
    }


//    GLFWwindow* glfw_main_window(const char* window_title /*= nullptr*/)
//    {
//        if (window_title == nullptr)
//            return _glfw_main_window;
//
//        FOUNDATION_ASSERT(_glfw_main_window == nullptr);
//
//        // Setup window
//        glfwSetErrorCallback(glfw_log_error);
//        if (!glfwInit())
//            return nullptr;
//
//        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
//
//#if FOUNDATION_PLATFORM_MACOS
//        glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_FALSE);
//        glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, GLFW_FALSE);
//#else
//        glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
//#endif
//
//        GLFWwindow* window = glfw_create_window_geometry(window_title);
//        if (window == nullptr)
//            return nullptr;
//
//        const application_t* app = environment_application();
//        string_const_t version_string = string_from_version_static(app->version);
//        glfwSetWindowTitle(window, string_format_static_const("%s v.%.*s", window_title, STRING_FORMAT(version_string)));
//        glfw_set_window_main_icon(window);
//
//        // Set global window handles if not set already.
//        if (_glfw_main_window == nullptr)
//            _glfw_main_window = window;
//
//        if (_window_handle == 0)
//            _window_handle = glfw_platform_window_handle(window);
//
//#if 1 //!BUILD_DEBUG
//        // As soon as the request to close the window is initiate we hide it in order to 
//        // give the impression that the application is already close, but in fact, the shutdown sequence
//        // is still running.
//        // FIXME: This is a hack, we should speed up the shutdown sequence in most cases
//        glfwSetWindowCloseCallback(window, [](GLFWwindow* window)
//            {
//                if (glfwWindowShouldClose(window))
//                {
//                    log_infof(0, STRING_CONST("Closing application..."));
//                    glfwHideWindow(window);
//                }
//            });
//#endif
//
//        return window;
//    }
//
//
//    void glfw_show_wait_cursor(GLFWwindow* window)
//    {
//#if FOUNDATION_PLATFORM_WINDOWS
//        HCURSOR cursor = LoadCursor(NULL, IDC_WAIT);
//        SetClassLongPtr(window ? (HWND)glfw_platform_window_handle(window) : NULL, GCLP_HCURSOR, (LONG_PTR)cursor);
//#else
//        FOUNDATION_UNUSED(window);
//#endif
//    }
//
//    void glfw_show_normal_cursor(GLFWwindow* window)
//    {
//        if (_glfw_default_cursor == nullptr)
//            _glfw_default_cursor = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
//
//#if FOUNDATION_PLATFORM_WINDOWS
//        HCURSOR cursor = LoadCursor(NULL, IDC_ARROW);
//        SetClassLongPtr(window ? (HWND)glfw_platform_window_handle(window) : NULL, GCLP_HCURSOR, (LONG_PTR)cursor);
//#else
//        if (window)
//            glfwSetCursor(window, _glfw_default_cursor);
//#endif
//    }
//
//    void glfw_request_close_window(GLFWwindow* window)
//    {
//        FOUNDATION_ASSERT(window);
//
//        glfwSetWindowShouldClose(window, GLFW_TRUE);
//#if 1 //!BUILD_DEBUG
//        if (glfwGetError(nullptr) == GLFW_NO_ERROR)
//            glfwHideWindow(window);
//#endif
//    }
//
//    float glfw_get_window_scale(GLFWwindow* window)
//    {
//        float scale = 1.0f;
//        if (window)
//        {
//            GLFWmonitor* monitor = glfw_find_window_monitor(window);
//            float scale_y = 1.0f;
//            glfwGetMonitorContentScale(monitor, &scale, &scale_y);
//        }
//        return scale;
//    }
//
//    float glfw_current_window_scale()
//    {
//        return glfw_get_window_scale(_glfw_main_window);
//    }



    void GlfwWindow::init() {

        //设置错误回调显示报错信息
        glfwSetErrorCallback(glfwLogError);

        if (!glfwInit())
        {
            throw std::runtime_error("Cannot initialize GLFW.");
        }

        // 在主线程进行渲染，不另开子线程
        bgfx::renderFrame();

        bgfx::Init bgfxInit;
        // 自动选择适合平台的默认呈现后端
        bgfxInit.type = bgfx::RendererType::Count;

        // TODO

        int width, height;

        glfwGetFramebufferSize(window,&width, &height);
        bgfxInit.resolution.width = static_cast<uint32_t>(width);
        bgfxInit.resolution.height = static_cast<uint32_t>(height);
        bgfxInit.resolution.reset = BGFX_RESET_VSYNC;
        bgfxInit.debug = false;
        bgfxInit.profile = false;

        if (!bgfx::init(bgfxInit))
        {
            LOG_ERROR("Bgfx Init error!");
        }

        glfwMakeContextCurrent(window);
    }


    void GlfwWindow::shutdown() {

    }


    void GlfwWindow::setTitle(const char* title) {

    }

    void GlfwWindow::setFullScreen(bool flag) {

    }

    void GlfwWindow::setResizeable(bool flag)
    {
    }

    void GlfwWindow::setSize(uint16_t width, uint16_t height) {

    }

    void GlfwWindow::setWindowIcon(const char* iconFilePath) const {

    }

    void GlfwWindow::setMouseVisible(bool isVisible, uint32_t xMouse, uint32_t yMouse) {

    }


    void GlfwWindow::update() {

    }


    void GlfwWindow::close() {

    }

}
