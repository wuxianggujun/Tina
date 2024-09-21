//
// Created by wuxianggujun on 2024/7/16.
//

#ifndef TINA_CORE_PLATFORM_HPP
#define TINA_CORE_PLATFORM_HPP

#include <bx/platform.h>

#if  BX_PLATFORM_ANDROID
#define TINA_PLATFORM_ANDROID
#define TINA_PLATFORM_NAME "Android"
#elif BX_PLATFORM_WINDOWS
#define TINA_PLATFORM_WINDOWS
#define TINA_PLATFORM_NAME "Windows"
#elif BX_PLATFORM_LINUX
#define TINA_PLATFORM_LINUX
#define TINA_PLATFORM_NAME "Linux"
#elif BX_PLATFORM_OSX
#define TINA_PLATFORM_OSX
#define TINA_PLATFORM_NAME "OS"
#else
#error "Unknown platform!"
#define TINA_PLATFORM_NAME "Unknown"
#endif

#if defined(TINA_PLATFORM_LINUX)
#if defined(TINA_CONFIG_USE_WAYLAND)
    #define GLFW_EXPOSE_NATIVE_WAYLAND
#else
    #define GLFW_EXPOSE_NATIVE_X11
    #define GLFW_EXPOSE_NATIVE_GLX
#endif

#elif defined(TINA_PLATFORM_OSX)
    #define GLFW_EXPOSE_NATIVE_COCOA
    #define GLFW_EXPOSE_NATIVE_NSGL
#elif defined(TINA_PLATFORM_WINDOWS)
    #define GLFW_EXPOSE_NATIVE_WIN32
    #define GLFW_EXPOSE_NATIVE_WGL
#endif


#endif //TINA_CORE_PLATFORM_HPP
