//
// Created by wuxianggujun on 2024/4/27.
//

#ifndef TINA_FRAMEWORK_WINDOW_HPP
#define TINA_FRAMEWORK_WINDOW_HPP

#include <cstdint>

#include <bx/bx.h>
#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#include <GLFW/glfw3.h>

#include <GLFW/glfw3native.h>

namespace Tina {

    class Window {

    public:
        explicit Window(const char *title, size_t width, size_t height);

        ~Window();

        bool initialize();

        void destroy();

        void update();

        [[nodiscard]] bool shouldClose() const;

    private:
        GLFWwindow *window = nullptr;
        size_t windowWidth = 0;
        size_t windowHeight = 0;
        const char *windowTitle = nullptr;
    };

} // Tina

#endif //TINA_FRAMEWORK_WINDOW_HPP
