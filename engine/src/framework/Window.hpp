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
#include <string>
#include <utility>
#include <filesystem>

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
        explicit Window(const WindowProps &props);

        ~Window();

        bool initialize(const WindowProps &props);

        void destroy();

        void update();

        void setIcon(const std::filesystem::path &iconPath);

        void setVSync(bool enabled);

        void setCursorPosition(double xPos,double yPos);
        void setWindowMode(WindowMode mode, size_t width = 0, size_t height = 0);

        void maximizeWindow();
        void restoreWindow();

        [[nodiscard]] bool shouldClose() const;

        [[nodiscard]] size_t getWidth() const { return windowData.width; }

        [[nodiscard]] size_t getHeight() const { return windowData.height; }

        [[nodiscard]] size_t getPosX() const { return windowData.posX; }

        [[nodiscard]] size_t getPosY() const { return windowData.posY; }

    private:
        struct WindowData {
            std::string title;
            size_t width, height, posX, posY;
            bool vSync;
            WindowMode windowMode;
            bool maximized;
        };

        GLFWwindow *window;
        GLFWvidmode videoMode;
        WindowData windowData;

        struct WindowModeParams {
            size_t width, height;
            size_t xPos, yPos;
        };

        WindowModeParams oldWindowModeParams;
    };

} // Tina

#endif //TINA_FRAMEWORK_WINDOW_HPP
