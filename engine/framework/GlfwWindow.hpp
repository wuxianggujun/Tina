#ifndef TINA_FRAMEWORK_GLFWWINDOW_HPP
#define TINA_FRAMEWORK_GLFWWINDOW_HPP

#include <cstdint>
#include <bx/bx.h>
#include <bx/spscqueue.h>
#include <bx/thread.h>
#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#include <GLFW/glfw3.h>
#if BX_PLATFORM_LINUX
#define GLFW_EXPOSE_NATIVE_X11
#elif BX_PLATFORM_WINDOWS
#define GLFW_EXPOSE_NATIVE_WIN32
#elif BX_PLATFORM_OSX
#define GLFW_EXPOSE_NATIVE_COCOA
#endif
#include <GLFW/glfw3native.h>

// We gather version tests as define in order to easily see which features are version-dependent.
#define GLFW_VERSION_COMBINED           (GLFW_VERSION_MAJOR * 1000 + GLFW_VERSION_MINOR * 100 + GLFW_VERSION_REVISION)
#ifdef GLFW_RESIZE_NESW_CURSOR          // Let's be nice to people who pulled GLFW between 2019-04-16 (3.4 define) and 2019-11-29 (cursors defines) // FIXME: Remove when GLFW 3.4 is released?
#define GLFW_HAS_NEW_CURSORS            (GLFW_VERSION_COMBINED >= 3400) // 3.4+ GLFW_RESIZE_ALL_CURSOR, GLFW_RESIZE_NESW_CURSOR, GLFW_RESIZE_NWSE_CURSOR, GLFW_NOT_ALLOWED_CURSOR
#else
#define GLFW_HAS_NEW_CURSORS            (0)
#endif
#define GLFW_HAS_GAMEPAD_API            (GLFW_VERSION_COMBINED >= 3300) // 3.3+ glfwGetGamepadState() new api
#define GLFW_HAS_GETKEYNAME             (GLFW_VERSION_COMBINED >= 3200) // 3.2+ glfwGetKeyName()

namespace Tina {

    class GLFWwindow;
    class GLFWmonitor;

    class GlfwWindow {

    private:
        static GLFWwindow* glfwMainWindow(const char* windowTitle = nullptr);
        static void glfwSetWindowCenter(GLFWwindow* window);
        static GLFWwindow* glfwCreateWindowGeometry(const char* windowTitle);
        static void glfwSaveWindowGeometry(GLFWwindow* window);
      
        static GLFWmonitor* glfwFindWindowMonitor(GLFWwindow* window);
        static GLFWmonitor* glfwFindWindowMonitor(int windowX, int windowY);

        static bool glfwIsWindowFoused(GLFWwindow* window);
        static bool glfwIsAnyMouseButtonDown(GLFWwindow* window);
        static int glfwTranslateUntranslatedKey(int key,int scanCode);
        static int glfwKeyToModifier(int key);
        static void glfwLogError(int error,const char* description);
        static void glfwSetWindowMainIcon(GLFWwindow* window);
    
        static void* glfwPlatformWindowHandle(GLFWwindow* window);
        static bool glfwGetWindowMonitorSize(GLFWwindow* window,int* monitorWidth,int* monitorHegiht);
        static bool glfwGetWindowMonitorSize(int windowX,int windowY,int* monitorWidth,int* monitorHegiht);
        static void glfwShowWaitCursor(GLFWwindow* window);
   
        static void glfwShowNormalCursor(GLFWwindow* window);

        struct GLFWWaitCursorScope
        {
            GLFWwindow* window;
            bool isCursorSet;
            GLFWWaitCursorScope(GLFWwindow* _window)
                : window(_window),isCursorSet(false)
            {
                if (_window != nullptr)
                {
                    glfwShowWaitCursor(window);
                    isCursorSet = true;
                }
            }

            GLFWWaitCursorScope()
                : GLFWWaitCursorScope(glfwMainWindow())
            {
            }

            ~GLFWWaitCursorScope()
            {
                if (isCursorSet)
                {
                    glfwShowNormalCursor(window);
                }
            }
        };

        static void glfwRequestCloseWindow(GLFWwindow* window);
 
        static float glfwGetWindowScale(GLFWwindow* window);
  
        static float glfwCurrentWindowScale();

    public:
        static void init();
        static void shutdown();

    public:

        void setTitle(const char* title);
        void setFullScreen(bool flag);
        void setResizeable(bool flag);
        void setSize(uint16_t width, uint16_t height);
        void setWindowIcon(const char* iconFilePath) const;
        void setMouseVisible(bool isVisible,uint32_t xMouse,uint32_t yMouse);

        void update();

        bool shouldClose() const{
            return isClosed;
        }

        void close();

    private:

        bool isClosed = false;
    };

}

#endif
