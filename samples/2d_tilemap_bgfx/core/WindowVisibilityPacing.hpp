#pragma once

#include <cstdint>

namespace Tina::Sample2D {

inline constexpr std::uint32_t MinimumProductUiVisibilityMilliseconds = 2200;

[[nodiscard]] constexpr bool useProductUiVisibilityPacing(
    std::uint32_t requestedFrameDelayMilliseconds) noexcept
{
    return requestedFrameDelayMilliseconds == 0;
}

[[nodiscard]] constexpr std::uint32_t productUiTargetElapsedMilliseconds(
    std::uint64_t completedFrames, std::uint64_t targetFrameCount) noexcept
{
    if (targetFrameCount == 0 || completedFrames >= targetFrameCount)
    {
        return MinimumProductUiVisibilityMilliseconds;
    }
    const long double progress =
        static_cast<long double>(completedFrames) / static_cast<long double>(targetFrameCount);
    return static_cast<std::uint32_t>(
        progress * static_cast<long double>(MinimumProductUiVisibilityMilliseconds));
}

} // namespace Tina::Sample2D
