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

#include <GLFW/glfw3.h>

namespace Tina {

    class GlfwUtils {
    public:
        /*static GLFWwindow* glfwMainWindow(const char* windowTitle = nullptr);
        static void glfwSetWindowCenter(GLFWwindow* window);
        static GLFWwindow* glfwCreateWindowGeometry(const char* windowTitle);
        static void glfwSaveWindowGeometry(GLFWwindow* window);

        static GLFWmonitor* glfwFindWindowMonitor(GLFWwindow* window);
        static GLFWmonitor* glfwFindWindowMonitor(int windowX, int windowY);

        static bool glfwIsWindowFoused(GLFWwindow* window);
        static bool glfwIsAnyMouseButtonDown(GLFWwindow* window);
        static int glfwTranslateUntranslatedKey(int key, int scanCode);
        static int glfwKeyToModifier(int key);
        */
        static void glfwLogError(int error, const char* description);
        /*static void glfwSetWindowMainIcon(GLFWwindow* window);*/

        static void* getNativeWindowHandle(GLFWwindow* window);

        /*     static void* glfwPlatformWindowHandle(GLFWwindow* window);
             static bool glfwGetWindowMonitorSize(GLFWwindow* window, int* monitorWidth, int* monitorHeight);
             static bool glfwGetWindowMonitorSize(int windowX, int windowY, int* monitorWidth, int* monitorHeight);
             static void glfwShowWaitCursor(GLFWwindow* window);

             static void glfwShowNormalCursor(GLFWwindow* window);*/

             /* struct GLFWWaitCursorScope
              {
                  GLFWwindow* window;
                  bool isCursorSet;
                  GLFWWaitCursorScope(GLFWwindow* _window)
                      : window(_window), isCursorSet(false)
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
              };*/
    };

}




#endif
