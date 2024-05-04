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
        Window(const char* title,size_t width,size_t height);
        ~Window() =delete;

        bool initialize();
        void close();
        void update();
        [[nodiscard]] bool shouldClose() const;

    private:
        GLFWwindow* window{};
        size_t windowWidth = 0;
        size_t windowHeight = 0;
        const char* windowTitle = nullptr;
    };

} // Tina

#endif //TINA_FRAMEWORK_WINDOW_HPP
