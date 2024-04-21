#ifndef TINA_FRAMEWORK_GLFWWINDOW_HPP
#define TINA_FRAMEWORK_GLFWWINDOW_HPP

#include <cstdint>


namespace Tina {

    class GlfwWindow {
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
