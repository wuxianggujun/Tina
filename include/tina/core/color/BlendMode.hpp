#pragma once

#include <tina/core/base/Types.hpp>

namespace Tina::Core {

// GPU compositor mode for already-transformed, premultiplied coverage.
// ColorTransform multiply/add is per-vertex math and is never a substitute
// for this: additive glow must add premultiplied RGB into the destination.
enum class BlendMode : u8 {
    PremultipliedAlpha = 0,
    Additive = 1,
};

[[nodiscard]] constexpr bool isSupportedBlendMode(BlendMode blendMode) noexcept
{
    return blendMode == BlendMode::PremultipliedAlpha || blendMode == BlendMode::Additive;
}

} // namespace Tina::Core
