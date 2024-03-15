//
// Created by 33442 on 2024/3/15.
//

#ifndef PLATFORMDETECTION_HPP
#define PLATFORMDETECTION_HPP

namespace Tina
{
#if defined(WIN32) || defined(_WIN32) || defined(_WIN32_)|| defined(WIN64) || defined(_WIN64) || defined(_WIN64_)
    #define PLATFORM_WINDOWS 1 // Windows平台
#elif defined(ANDROID) || defined(_ANDROID_)
    #define  PLATFORM_ANDROID 1 // Android平台
#elif  defined(__linux__) || defined(__linux) || defined(linux) || defined(__linux__)
    #define  PLATFORM_LINUX 1 // Linux平台
#elif defined(__APPLE__) || defined(TARGET_OS_IPHONE) || defined(TARGET_IPHONE_SIMULATOR) || defined(TARGET_OS_MAC) || defined(TARGET_OS_MACOS)
    #define PLATFORM_MAC 1 // Mac平台
#else
    #define PLATFORM_UNKNOWN 1 // 未知平台
#endif
}

#endif //PLATFORMDETECTION_HPP
