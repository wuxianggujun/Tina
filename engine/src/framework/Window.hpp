//
// Created by wuxianggujun on 2024/4/27.
//

#ifndef TINA_FRAMEWORK_WINDOW_HPP
#define TINA_FRAMEWORK_WINDOW_HPP

#include <cstdint>

class GLFWwindow;

namespace Tina {

    class Window {

    public:
        Window(size_t width,size_t height,const char* title);
        ~Window() =delete;

        void initialize();
        void close();


    private:
        GLFWwindow* window{};
        size_t windowWidth = 0;
        size_t windowHeight = 0;
        const char* windowTitle = nullptr;
    };

} // Tina

#endif //TINA_FRAMEWORK_WINDOW_HPP
