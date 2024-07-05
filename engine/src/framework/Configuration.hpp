//
// Created by wuxianggujun on 2024/4/27.
//

#ifndef TINA_FRAMEWORK_CONFIGURATION_HPP
#define TINA_FRAMEWORK_CONFIGURATION_HPP

#include "framework/GraphicsBackend.hpp"
#include <cstdint>

namespace Tina {
    class Configuration {
    public:
        explicit Configuration(const char *title = "Tina", const char *icon = "", uint16_t width = 800, uint16_t height = 600,
                      bool fullScreen = false,
                      GraphicsBackend backend = GraphicsBackend::Direct3DX11): windowTitle(title), iconFilePath(icon),
                                                windowWidth(width),
                                                windowHeight(height), useFullScreen(fullScreen),
                                                graphicsBackend(backend) {
        }

        ~Configuration() = default;

    public:
        const char *windowTitle{};
        const char *iconFilePath{};
        uint16_t windowWidth{};
        uint16_t windowHeight{};
        bool useFullScreen{};
        GraphicsBackend graphicsBackend{};
    };
}

#endif //TINA_FRAMEWORK_CONFIGURATION_HPP
