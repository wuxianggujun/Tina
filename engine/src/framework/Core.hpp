//
// Created by wuxianggujun on 2024/5/13.
//

#ifndef TINA_FRAGMENT_CORE_HPP
#define TINA_FRAGMENT_CORE_HPP

#include <memory>

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

template<class T, class ... Args>
constexpr Scope<T> createScope(Args &&... args) {
    return std::make_unique<T>(std::forward<Args>(args)...);
}

template<class T>
using Ref = std::shared_ptr<T>;

template<class T, class ... Args>
constexpr Ref<T> createRef(Args &&... args) {
    return std::make_shared<T>(std::forward<Args>(args)...);
}


#endif //TINA_FRAGMENT_CORE_HPP
