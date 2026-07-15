#pragma once

#include <csignal>
#include <cstdlib>

#if defined(_MSC_VER)
#define TINA_LIBRARY_EXPORT __declspec(dllexport)
#define TINA_LIBRARY_IMPORT __declspec(dllimport)
#define TINA_FORCE_INLINE __forceinline
#define TINA_NO_INLINE __declspec(noinline)
#define TINA_RESTRICT __restrict
#elif defined(__GNUC__) || defined(__clang__)
#define TINA_LIBRARY_EXPORT __attribute__((visibility("default")))
#define TINA_LIBRARY_IMPORT
#define TINA_FORCE_INLINE inline __attribute__((always_inline))
#define TINA_NO_INLINE __attribute__((noinline))
#define TINA_RESTRICT __restrict__
#else
#define TINA_LIBRARY_EXPORT
#define TINA_LIBRARY_IMPORT
#define TINA_FORCE_INLINE inline
#define TINA_NO_INLINE
#define TINA_RESTRICT
#endif

// Tina Core is currently a static library. The opt-in branch keeps a future shared build ABI-safe.
#if defined(TINA_CORE_SHARED)
#if defined(TINA_CORE_BUILDING)
#define TINA_CORE_API TINA_LIBRARY_EXPORT
#else
#define TINA_CORE_API TINA_LIBRARY_IMPORT
#endif
#else
#define TINA_CORE_API
#endif

namespace Tina::Core {

TINA_FORCE_INLINE void debugBreak() noexcept
{
#if defined(_MSC_VER)
    __debugbreak();
#elif defined(__has_builtin)
#if __has_builtin(__builtin_debugtrap)
    __builtin_debugtrap();
#elif defined(SIGTRAP)
    std::raise(SIGTRAP);
#else
    std::abort();
#endif
#elif defined(SIGTRAP)
    std::raise(SIGTRAP);
#else
    std::abort();
#endif
}

} // namespace Tina::Core

#define TINA_DEBUG_BREAK ::Tina::Core::debugBreak()
