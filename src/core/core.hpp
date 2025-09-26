//
// Created by wuxianggujun on 25-9-26.
//

#pragma once

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#ifndef _WIN32
#include <signal.h>
#endif

#if !defined(_WIN32) && !defined(__linux__)
#error Platform not supported
#endif

#ifndef ASSERT

#ifndef TINA_DEBUG
#ifdef _WIN32
#define TINA_DEBUG_BREAK __debugbreak()
#else
#define TINA_DEBUG_BREAK raise(SIGTRAP)
#endif
#define ASSERT(x) do { const volatile bool tina_assert_value__ = !(x); if (tina_assert_value__) TINA_DEBUG_BREAK(); } while(false)
#else
#if defined _MSC_VER && !defined __clang__
#define ASSERT(x) __assume(x)
#else
#define ASSERT(x) {false? (void)(x) : (void)0;}
#endif
#endif
#endif

namespace Tina
{
#ifdef MAX_PATH
#undef MAX_PATH
#endif

    enum
    {
        MAX_PATH = 260
    };

    using i8 = char;
    using u8 = unsigned char;
    using i16 = short;
    using u16 = unsigned short;
    using i32 = int;
    using u32 = unsigned int;
#ifdef _WIN32
    using i64 = long long;
    using u64 = unsigned long long;
#else
    using i64 = long;
    using u64 = unsigned long;
#endif
    using uintptr = u64;

    static_assert(sizeof(uintptr) == sizeof(void*), "Incorrect size of uintptr");
    static_assert(sizeof(i64) == 8, "Incorrect size of i64");
    static_assert(sizeof(i32) == 4, "Incorrect size of i32");
    static_assert(sizeof(i16) == 2, "Incorrect size of i16");
    static_assert(sizeof(i8) == 1, "Incorrect size of i8");

    template <typename T, u32 count>
    constexpr u32 lengthOf(const T (&)[count])
    {
        return count;
    };

    template <bool, class T>
    struct EnableIf
    {
    };

    template <class T>
    struct EnableIf<true, T>
    {
        using Type = T;
    };

    template <class T>
    inline constexpr bool is_enum_v = __is_enum(T);

    template <typename T, typename EnableIf<is_enum_v<T>, int>::Type = 0>
    constexpr T operator |(T a, T b) { return T(u64(a) | u64(b)); }

    template <typename T, typename EnableIf<is_enum_v<T>, int>::Type = 0>
    constexpr T operator &(T a, T b) { return T(u64(a) & u64(b)); }

    template <typename T, typename EnableIf<is_enum_v<T>, int>::Type = 0>
    constexpr T operator ^(T a, T b) { return T(u64(a) ^ u64(b)); }

    template <typename T, typename EnableIf<is_enum_v<T>, int>::Type = 0>
    constexpr void operator |=(T& a, T b) { a = T(u64(a) | u64(b)); }

    template <typename T, typename EnableIf<is_enum_v<T>, int>::Type = 0>
    constexpr void operator &=(T& a, T b) { a = T(u64(a) & u64(b)); }

    template <typename T, typename EnableIf<is_enum_v<T>, int>::Type = 0>
    constexpr void operator ^=(T& a, T b) { a = T(u64(a) ^ u64(b)); }

    template <typename T, typename EnableIf<is_enum_v<T>, int>::Type = 0>
    constexpr T operator ~(T a) { return T(~u64(a)); }

    template <typename E>
    bool isFlagSet(E flags, E flag) { return ((u64)flags & (u64)flag); }

    template <typename E>
    void setFlag(E& flags, E flag, bool set)
    {
        if (set) flags = E((u64)flags | (u64)flag);
        else flags = E(u64(flags) & ~u64(flag));
    }

    // Math types using GLM
    using Vec2 = glm::vec2;
    using Vec3 = glm::vec3; 
    using Vec4 = glm::vec4;
    using IVec2 = glm::ivec2;
    using IVec3 = glm::ivec3;
    using IVec4 = glm::ivec4;
    
    // Convenience aliases
    using Point = IVec2;      // 2D point with integer coordinates
    using Size = IVec2;       // 2D size with integer dimensions
    using Position = Vec2;    // 2D position with float coordinates
    
    // Rectangle structure using GLM vectors
    struct Rect
    {
        Point position;       // Top-left corner
        Size size;           // Width and height
        
        Rect() = default;
        Rect(i32 x, i32 y, i32 w, i32 h) : position(x, y), size(w, h) {}
        Rect(const Point& pos, const Size& sz) : position(pos), size(sz) {}
        
        i32 x() const { return position.x; }
        i32 y() const { return position.y; }
        i32 width() const { return size.x; }
        i32 height() const { return size.y; }
        
        i32 right() const { return position.x + size.x; }
        i32 bottom() const { return position.y + size.y; }
        
        Point topLeft() const { return position; }
        Point topRight() const { return Point(right(), y()); }
        Point bottomLeft() const { return Point(x(), bottom()); }
        Point bottomRight() const { return Point(right(), bottom()); }
        
        bool contains(const Point& point) const
        {
            return point.x >= x() && point.x < right() && 
                   point.y >= y() && point.y < bottom();
        }
        
        bool intersects(const Rect& other) const
        {
            return !(right() <= other.x() || x() >= other.right() ||
                    bottom() <= other.y() || y() >= other.bottom());
        }
    };


#ifdef _WIN32
#define TINA_LIBRARY_EXPORT __declspec(dllexport)
#define TINA_LIBRARY_IMPORT __declspec(dllimport)
#define TINA_FORCE_INLINE __forceinline
#define TINA_RESTRICT __restrict
#else
#define TINA_LIBRARY_EXPORT __attribute__((visibility("default")))
#define TINA_LIBRARY_IMPORT
#define TINA_FORCE_INLINE __attribute__((always_inline)) inline
#define TINA_RESTRICT __restrict__
#endif

#ifdef STATIC_PLUGINS
#define TINA_CORE_API
#elif defined BUILDING_CORE
    #define TINA_CORE_API TINA_LIBRARY_EXPORT
#else
#define TINA_CORE_API TINA_LIBRARY_IMPORT
#endif

#ifdef _MSC_VER
#pragma warning(error : 4101)
#pragma warning(error : 4127)
#pragma warning(error : 4263)
#pragma warning(error : 4265)
#pragma warning(error : 4296)
#pragma warning(error : 4456)
#pragma warning(error : 4062)
#pragma warning(error : 5233)
#pragma warning(error : 5245)
#pragma warning(disable : 4251)
    // this is disabled because VS19 16.5.0 has false positives :(
#pragma warning(disable : 4724)
#if _MSC_VER == 1900
#pragma warning(disable : 4091)
#endif
#endif

#ifdef __clang__
#pragma clang diagnostic ignored "-Wreorder-ctor"
#pragma clang diagnostic ignored "-Wunknown-pragmas"
#pragma clang diagnostic ignored "-Wignored-pragma-optimize"
#pragma clang diagnostic ignored "-Wmissing-braces"
#pragma clang diagnostic ignored "-Wchar-subscripts"
#endif
}
