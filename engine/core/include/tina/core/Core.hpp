#pragma once

#include <bx/bx.h>

// Platform detection
#if BX_PLATFORM_WINDOWS
#define TINA_PLATFORM_WINDOWS
#elif BX_PLATFORM_LINUX
    #define TINA_PLATFORM_LINUX
#elif BX_PLATFORM_OSX
    #define TINA_PLATFORM_MACOS
#else
    #error "Unsupported platform!"
#endif

// DLL export/import macros
#ifdef TINA_PLATFORM_WINDOWS
#ifdef TINA_SHARED_LIB
#ifdef TINA_CORE_EXPORTS
            #define TINA_CORE_API __declspec(dllexport)
#else
            #define TINA_CORE_API __declspec(dllimport)
#endif
#else
#define TINA_CORE_API
#endif
#else
    #define TINA_CORE_API
#endif

// Debug macros
#ifdef NDEBUG
    #define TINA_RELEASE
#else
#define TINA_DEBUG
#endif

// Assertion macros
#ifdef TINA_DEBUG
    #define TINA_ENGINE_ASSERT(x, ...) { if(!(x)) { Tina::Log::getEngineLogger()->error( __VA_ARGS__); } }

    #define TINA_ASSERT(x, ...) { if(!(x)) { Tina::Log::getApplicationLogger()->error( __VA_ARGS__); } }

#else
    #define TINA_ASSERT(x, ...)

    #define TINA_CORE_ASSERT(x, ...)
#endif
#include <memory>
#include <functional>

namespace Tina
{
    // 智能指针的简单封装
    template <typename T>
    using SharedPtr = std::shared_ptr<T>;

    template <typename T>
    using UniquePtr = std::unique_ptr<T>;

    template <typename T>
    using WeakPtr = std::weak_ptr<T>;

    // 工具函数封装
    template <typename T, typename... Args>
    inline SharedPtr<T> MakeShared(Args&&... args)
    {
        return std::make_shared<T>(std::forward<Args>(args)...);
    }

    template <typename T, typename... Args>
    inline UniquePtr<T> MakeUnique(Args&&... args)
    {
        return std::make_unique<T>(std::forward<Args>(args)...);
    }

    // 带自定义删除器的智能指针创建函数
    template <typename T>
    inline SharedPtr<T> MakeShared(T* ptr, std::function<void(T*)> deleter)
    {
        return SharedPtr<T>(ptr, deleter);
    }

    template <typename T>
    inline UniquePtr<T> MakeUnique(T* ptr, std::function<void(T*)> deleter)
    {
        return UniquePtr<T>(ptr, deleter);
    }
} // namespace Tina
