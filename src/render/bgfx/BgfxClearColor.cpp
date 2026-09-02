#include "BgfxClearColor.hpp"

#include <algorithm>
#include <cmath>

namespace Tina::Render::Bgfx {
namespace {

[[nodiscard]] u8 quantize(float unitRange) noexcept
{
    return static_cast<u8>(std::lround(std::clamp(unitRange, 0.0F, 1.0F) * 255.0F));
}

} // namespace

u8 encodeSrgbComponent(float linear) noexcept
{
    const float clamped = std::clamp(linear, 0.0F, 1.0F);
    const float encoded = clamped <= 0.0031308F
                              ? clamped * 12.92F
                              : 1.055F * std::pow(clamped, 1.0F / 2.4F) - 0.055F;
    return quantize(encoded);
}

u32 packClearRgba(const RenderLinearColor& color) noexcept
{
    // Alpha is coverage, not light, so it is quantized without the transfer function.
    return (static_cast<u32>(encodeSrgbComponent(color.red)) << 24U) |
           (static_cast<u32>(encodeSrgbComponent(color.green)) << 16U) |
           (static_cast<u32>(encodeSrgbComponent(color.blue)) << 8U) |
           static_cast<u32>(quantize(color.alpha));
}

} // namespace Tina::Render::Bgfx
