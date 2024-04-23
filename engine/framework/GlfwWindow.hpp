#ifndef TINA_FRAMEWORK_GLFWWINDOW_HPP
#define TINA_FRAMEWORK_GLFWWINDOW_HPP

#include "GlfwUtils.hpp"

class GLFWwindow;
class GLFWmonitor;

namespace Tina {

  
    class GlfwWindow {

    public:
        explicit GlfwWindow() = default;

        virtual ~GlfwWindow();

        bool initialize(const char* title, uint16_t width, uint16_t height, const char* iconFilePath = nullptr, bool useFullScreen = false);

    public:

        void setTitle(const char* title);
        void setFullScreen(bool flag);
        void setResizeable(bool flag);
        void setSize(uint16_t width, uint16_t height);
        void setWindowIcon(const char* iconFilePath) const;
        void setMouseVisible(bool isVisible,uint32_t xMouse,uint32_t yMouse);

        void update();

        bool shouldClose(){
            isClosed = !glfwWindowShouldClose(window);
            return isClosed;
        }

        void close();

    private:
        static GLFWwindow* window;
        bool isClosed = false;
    };

}

#endif
