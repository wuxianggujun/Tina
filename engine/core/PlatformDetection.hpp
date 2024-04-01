//
// Created by 33442 on 2024/3/15.
//

#ifndef PLATFORMDETECTION_HPP
#define PLATFORMDETECTION_HPP

#include <boost/predef.h>

#if BOOST_OS_WINDOWS
#define TINA_PLATFORM_WINDOWS 1
#elif BOOST_OS_LINUX
#define TINA_PLATFORM_LINUX
#elif BOOST_PLAT_ANDROID
#define TINA_PLATFORM_ANDROID
#elif BOOST_OS_IOS
#define TINA_PLATFORM_IOS
#endif

//#if defined(WIN32) || defined(_WIN32) || defined(_WIN32_)|| defined(WIN64) || defined(_WIN64) || defined(_WIN64_)
//    #define TINA_PLATFORM_WINDOWS 1 
//#elif defined(ANDROID) || defined(_ANDROID_)
//    #define  TINA_PLATFORM_ANDROID 1 
//#elif  defined(__linux__) || defined(__linux) || defined(linux) || defined(__linux__)
//    #define  TINA_PLATFORM_LINUX 1 
//#elif defined(__APPLE__) || defined(TARGET_OS_IPHONE) || defined(TARGET_IPHONE_SIMULATOR) || defined(TARGET_OS_MAC) || defined(TARGET_OS_MACOS)
//    #define TINA_PLATFORM_MAC 1 
//#else
//    #define TINA_PLATFORM_UNKNOWN 1
//#endif

#endif //PLATFORMDETECTION_HPP
