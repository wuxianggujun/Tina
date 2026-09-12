#pragma once

#include <tina/core/base/Types.hpp>

namespace Tina::Sample2D {

inline constexpr Tina::Core::u32 MinimumProductUiVisibilityMilliseconds = 2200;

[[nodiscard]] constexpr bool useProductUiVisibilityPacing(
    Tina::Core::u32 requestedFrameDelayMilliseconds) noexcept
{
    return requestedFrameDelayMilliseconds == 0;
}

[[nodiscard]] constexpr Tina::Core::u32 productUiTargetElapsedMilliseconds(
    Tina::Core::u64 completedFrames, Tina::Core::u64 targetFrameCount) noexcept
{
    if (targetFrameCount == 0 || completedFrames >= targetFrameCount)
    {
        return MinimumProductUiVisibilityMilliseconds;
    }
    const long double progress =
        static_cast<long double>(completedFrames) / static_cast<long double>(targetFrameCount);
    return static_cast<Tina::Core::u32>(
        progress * static_cast<long double>(MinimumProductUiVisibilityMilliseconds));
}

} // namespace Tina::Sample2D
