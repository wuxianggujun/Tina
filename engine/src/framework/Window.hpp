//
// Created by wuxianggujun on 2024/4/27.
//

#ifndef TINA_FRAMEWORK_WINDOW_HPP
#define TINA_FRAMEWORK_WINDOW_HPP


#include <iostream>

#include <bx/bx.h>
#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#include <GLFW/glfw3.h>
#include <bitset>

#if BX_PLATFORM_LINUX
#define GLFW_EXPOSE_NATIVE_X11
#elif BX_PLATFORM_WINDOWS
#define GLFW_EXPOSE_NATIVE_WIN32
#elif BX_PLATFORM_OSX
#define GLFW_EXPOSE_NATIVE_COCOA
#endif

#include <GLFW/glfw3native.h>
#include "framework/render/RenderContext.hpp"
#include "Configuration.hpp"
#include "math/Vector2i.hpp"
#include "tool/GlfwTool.hpp"

namespace Tina {
    enum class WindowMode {
        WINDOWED = 0, // Windowed mode
        FULLSCREEN = 1, // Fullscreen mode
        BORDERLESS = 2 // Borderless windowed mode
    };

    struct WindowProps {
        std::string title;
        size_t width;
        size_t height;

        size_t posX;
        size_t posY;

        WindowMode windowMode;

        bool maximized{};

        explicit WindowProps(std::string title = "Tina",
                             size_t width = 1920, size_t height = 1080, size_t posX = 20,
                             size_t posY = 50, WindowMode windowMode = WindowMode::WINDOWED,
                             bool maximized = false) : title(std::move(title)), width(width), height(height),
                                                       posX(posX), posY(posY),
                                                       windowMode(windowMode) {
        }
    };


    class Window {
    public:
        explicit Window(const Configuration &config);

        ~Window() = default;

        bool initialize();

        void setRenderContext(Ref<RenderContext> renderContext);

        void update();

        [[nodiscard]] void *getNativeWindowHandle();

        bool handleResize();

        void destroy();

        [[nodiscard]] bool isRunning() const;

        int fail{};

        GLFWwindow *window{};
        Vector2i windowSize;
        double xScale{};
        double yScale{};

        static bool keyStates[GLFW_KEY_LAST + 1];

    private:
        void *nativeWindowHandle{};
        const Configuration configuration;
        Ref<RenderContext> renderContext;
    };
} // Tina

#endif //TINA_FRAMEWORK_WINDOW_HPP
