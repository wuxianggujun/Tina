#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/id/AssetId.hpp>
#include <tina/ui/UIContent.hpp>
#include <tina/ui/UIPaint.hpp>

#include <compare>

namespace Tina::UI {

struct UIImagePixelExtent final {
    u32 width = 0;
    u32 height = 0;

    auto operator<=>(const UIImagePixelExtent&) const = default;
};

struct UIImagePixelRect final {
    u32 x = 0;
    u32 y = 0;
    u32 width = 0;
    u32 height = 0;

    auto operator<=>(const UIImagePixelRect&) const = default;
};

// Retained source metadata only. Asset lookup, frame pinning, and GPU handles
// belong to the Runtime/Render extraction boundary rather than UIContext.
struct UIImageSource final {
    Core::AssetId texture{};
    UIImagePixelRect sourcePixels{};
    UIImagePixelExtent texturePixelExtent{};
    UILogicalSize intrinsicLogicalSize{};

    auto operator<=>(const UIImageSource&) const = default;
};

enum class UIImageFit : u8 {
    Fill = 0,
    Contain,
    Cover,
    None,
};

enum class UIImageSampling : u8 {
    Linear = 0,
    Nearest,
};

struct UIImageContent final {
    UIImageSource source{};
    UIImageFit fit = UIImageFit::Contain;
    UIContentAlignment alignment{};
    UIStraightSrgba8Color tint = rgba8(255, 255, 255);
    UIImageSampling sampling = UIImageSampling::Linear;

    auto operator<=>(const UIImageContent&) const = default;
};

} // namespace Tina::UI
