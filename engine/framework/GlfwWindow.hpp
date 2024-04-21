#ifndef TINA_FRAMEWORK_GLFWWINDOW_HPP
#define TINA_FRAMEWORK_GLFWWINDOW_HPP

#include <cstdint>


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
