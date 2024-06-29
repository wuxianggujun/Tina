//
// Created by wuxianggujun on 2024/5/13.
//

#ifndef TINA_FRAGMENT_CORE_HPP
#define TINA_FRAGMENT_CORE_HPP

#include <memory>


#define TINA_USE_NAMESPACE using namespace tina;

#define TINA_COMPILER_MSVC 0
#define TINA_COMPILER_GCC 0
#define TINA_COMPILER_CLANG 0

#define TINA_PLATFORM_WINDOWS 0
#define TINA_PLATFORM_ANDROID 0
#define TINA_PLATFORM_IOS 0
#define TINA_PLATFORM_WINDOWS 0
#define TINA_PLATFORM_LINUX 0
#define TINA_PLATFORM_OSX 0
#define TINA_PLATFORM_EMSCRIPTEN 0

#if defined(_MSC_VER)
#undef TINA_COMPILER_MSVC
#define TINA_COMPILER_MSVC 1
#elif defined(__GNUC__)
#undef TINA_COMPILER_GCC
#define TINA_COMPILER_GCC 1
#elif defined(__clang__)
#undef TINA_COMPILER_CLANG
#define TINA_COMPILER_CLANG 1
#else
#error "Unsupported compiler"
#endif


#if defined(_WIN32) || defined(_WIN64)
#undef TINA_PLATFORM_WINDOWS
#define TINA_PLATFORM_WINDOWS 1
#elif defined(__ANDROID__)
#undef TINA_PLATFORM_ANDROID
#define TINA_PLATFORM_ANDROID 1
#elif defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE || TARGET_IPHONE_SIMULATOR || __ENVIRONMENT_IPHONE_OS_VERSION_MIN_REQUIRED__
#undef TINA_PLATFORM_IOS
#define TINA_PLATFORM_IOS 1
#else defined(__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__)
#undef TINA_PLATFORM_OSX
#define TINA_PLATFORM_OSX 1
#endif
#elif defined(__EMSCRIPTEN__)
#undef TINA_PLATFORM_EMSCRIPTEN
#define TINA_PLATFORM_EMSCRIPTEN 1
#elif defined(__linux__)
#undef TINA_PLATFORM_LINUX
#define TINA_PLATFORM_LINUX 1
#else
#error "Unsupported platform"
#endif

#if TINA_COMPILER_GCC
#define TINA_COMPILER_NAME "GCC"
#elif TINA_COMPILER_MSVC
#define TINA_COMPILER_NAME "MSVC"
#elif TINA_COMPILER_CLANG
#define TINA_COMPILER_NAME "Clang"
#endif

#if TINA_PLATFORM_ANDROID
#define TINA_PLATFORM_NAME "Android"
#elif TINA_PLATFORM_IOS
#define TINA_PLATFORM_NAME "iOS"
#elif TINA_PLATFORM_WINDOWS
#define TINA_PLATFORM_NAME "Windows"
#elif TINA_PLATFORM_LINUX
#define TINA_PLATFORM_NAME "Linux"
#elif TINA_PLATFORM_OSX
#define TINA_PLATFORM_NAME "OSX"
#elif TINA_PLATFORM_EMSCRIPTEN
#define TINA_PLATFORM_NAME "Html5"
#endif


#ifdef DEBUG
#if defined(_MSC_VER)
#define DEBUG_BREAK() __debugbreak()
#elif defined(__linux__)
#include <signal.h>
#if defined(SIGTRAP)
#define DEBUG_BREAK() raise(SIGTRAP)
#else
#define DEBUG_BREAK() raise(SIGABRT)
#endif
#elif defined(__APPLE__)
#define DEBUG_BREAK() __builtin_trap()
#else
#define DEBUG_BREAK()
#endif
#else
#define DEBUG_BREAK()
#endif // DEBUG


#ifdef ENGINE_BUILD_SHARED
#define ENGINE_API __declspec(dllexport)
#else
#define ENGINE_API
#endif


template<class T>
using Scope = std::unique_ptr<T>;

template<class T, class... Args>
constexpr Scope<T> createScope(Args &&... args) {
    return std::make_unique<T>(std::forward<Args>(args)...);
}

template<class T>
using Ref = std::shared_ptr<T>;

template<class T, class... Args>
constexpr Ref<T> createRef(Args &&... args) {
    return std::make_shared<T>(std::forward<Args>(args)...);
}


#endif //TINA_FRAGMENT_CORE_HPP
