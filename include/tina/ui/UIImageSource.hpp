#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/id/AssetId.hpp>
#include <tina/ui/UILayout.hpp>

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

struct UIImagePixelInsets final {
    u32 left = 0;
    u32 top = 0;
    u32 right = 0;
    u32 bottom = 0;

    auto operator<=>(const UIImagePixelInsets&) const = default;
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

enum class UIImageSampling : u8 {
    Linear = 0,
    Nearest,
};

} // namespace Tina::UI
