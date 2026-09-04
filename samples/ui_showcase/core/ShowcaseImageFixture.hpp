#pragma once

#include <tina/core/id/AssetId.hpp>
#include <tina/ui/UIImage.hpp>

#include <algorithm>
#include <array>
#include <cstddef>

namespace Tina::SampleUI {

inline constexpr Core::u32 ShowcaseAtlasWidth = 64;
inline constexpr Core::u32 ShowcaseAtlasHeight = 64;

[[nodiscard]] inline constexpr Core::AssetId showcaseAtlasAssetId() noexcept
{
    Core::AssetId::Bytes bytes{};
    bytes[0] = std::byte{0x54};
    bytes[1] = std::byte{0x49};
    bytes[2] = std::byte{0x4E};
    bytes[3] = std::byte{0x41};
    bytes[15] = std::byte{0x01};
    return *Core::AssetId::fromBytes(bytes);
}

[[nodiscard]] inline constexpr UI::UIImageSource showcaseAtlasSource(UI::UIImagePixelRect sourcePixels,
                                                                     UI::UILogicalSize intrinsicLogicalSize) noexcept
{
    return UI::UIImageSource{
        .texture = showcaseAtlasAssetId(),
        .sourcePixels = sourcePixels,
        .texturePixelExtent = {.width = ShowcaseAtlasWidth, .height = ShowcaseAtlasHeight},
        .intrinsicLogicalSize = intrinsicLogicalSize,
    };
}

using ShowcaseAtlasPixels = std::array<std::byte, ShowcaseAtlasWidth * ShowcaseAtlasHeight * 4U>;

constexpr void setShowcaseAtlasPixel(ShowcaseAtlasPixels& pixels, Core::u32 x, Core::u32 y, Core::u8 red,
                                     Core::u8 green, Core::u8 blue, Core::u8 alpha = 255) noexcept
{
    const Core::usize offset = (static_cast<Core::usize>(y) * ShowcaseAtlasWidth + x) * 4U;
    pixels[offset] = static_cast<std::byte>(red);
    pixels[offset + 1U] = static_cast<std::byte>(green);
    pixels[offset + 2U] = static_cast<std::byte>(blue);
    pixels[offset + 3U] = static_cast<std::byte>(alpha);
}

[[nodiscard]] inline constexpr ShowcaseAtlasPixels makeShowcaseAtlasPixels() noexcept
{
    ShowcaseAtlasPixels pixels{};

    // 16x16 lightning and close icons. White texels are tinted by each Image.
    for (Core::u32 y = 0; y < 16; ++y) {
        for (Core::u32 x = 0; x < 16; ++x) {
            const bool lightning = (y < 7U && x >= 7U - y / 2U && x <= 10U) ||
                                   (y >= 6U && y <= 9U && x >= 4U && x <= 11U) ||
                                   (y >= 9U && x >= 5U && x <= 8U + (y - 9U) / 2U);
            if (lightning) {
                setShowcaseAtlasPixel(pixels, x, y, 255, 255, 255);
            }

            const bool close = (x >= 3U && x <= 12U && y >= 3U && y <= 12U) &&
                               ((x > y ? x - y : y - x) <= 1U || ((x + y > 15U ? x + y - 15U : 15U - x - y) <= 1U));
            if (close) {
                setShowcaseAtlasPixel(pixels, x + 16U, y, 255, 255, 255);
            }
        }
    }

    // 32x32 inventory thumbnail with a small potion silhouette.
    for (Core::u32 y = 0; y < 32; ++y) {
        for (Core::u32 x = 0; x < 32; ++x) {
            const bool checker = ((x / 4U) + (y / 4U)) % 2U == 0U;
            setShowcaseAtlasPixel(pixels, x + 32U, y, checker ? 28U : 34U, checker ? 44U : 52U, checker ? 68U : 78U);
            const bool neck = x >= 13U && x <= 18U && y >= 5U && y <= 11U;
            const int dx = static_cast<int>(x) - 16;
            const int dy = static_cast<int>(y) - 19;
            const bool bottle = dx * dx + dy * dy <= 72 && y >= 10U && y <= 27U;
            if (neck || bottle) {
                const bool liquid = y >= 18U;
                setShowcaseAtlasPixel(pixels, x + 32U, y, liquid ? 74U : 196U, liquid ? 220U : 232U,
                                      liquid ? 176U : 240U, liquid ? 255U : 210U);
            }
        }
    }

    // 32x32 stretch-only NineSlice panel: 8px corners/edges and a quiet center.
    for (Core::u32 y = 0; y < 32; ++y) {
        for (Core::u32 x = 0; x < 32; ++x) {
            const Core::u32 edgeDistance = (std::min)(x, (std::min)(y, (std::min)(31U - x, 31U - y)));
            if (edgeDistance < 2U) {
                setShowcaseAtlasPixel(pixels, x, y + 32U, 92, 226, 196, 220);
            } else if (edgeDistance < 8U) {
                setShowcaseAtlasPixel(pixels, x, y + 32U, 34, 124, 116, 80);
            } else {
                setShowcaseAtlasPixel(pixels, x, y + 32U, 0, 0, 0, 0);
            }
        }
    }
    return pixels;
}

} // namespace Tina::SampleUI
