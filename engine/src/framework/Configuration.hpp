//
// Created by wuxianggujun on 2024/4/27.
//

#ifndef TINA_FRAMEWORK_CONFIGURATION_HPP
#define TINA_FRAMEWORK_CONFIGURATION_HPP

#include <cstdint>

namespace Tina{
    class Configuration{
    public:
        Configuration(const char* title,size_t width,size_t height,bool fullScreen = false):windowTitle(title),windowWidth(width),
            windowHeight(height),useFullScreen(fullScreen){
        }
        ~Configuration() = default;
    public:
        const char* windowTitle = "Tina";
        size_t windowWidth = 800;
        size_t windowHeight = 600;
        bool useFullScreen = false;
    };
}

#endif //TINA_FRAMEWORK_CONFIGURATION_HPP
