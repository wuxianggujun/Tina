#pragma once

#include <tina/core/id/AssetId.hpp>
#include <tina/ui/UIIcon.hpp>

#include <array>
#include <cstddef>

namespace Tina::SampleUI {

inline constexpr Core::u32 DesktopShellIconExtent = 16;
inline constexpr Core::u32 DesktopShellIconCount = 5;
inline constexpr Core::u32 DesktopShellIconAtlasWidth =
    DesktopShellIconExtent * DesktopShellIconCount;
inline constexpr Core::u32 DesktopShellIconAtlasHeight = DesktopShellIconExtent;

enum class DesktopShellIcon : Core::u8 {
    Play = 0,
    Save,
    Undo,
    Redo,
    Delete,
};

[[nodiscard]] inline constexpr Core::AssetId desktopShellIconAtlasAssetId() noexcept
{
    Core::AssetId::Bytes bytes{};
    bytes[0] = std::byte{0x54};
    bytes[1] = std::byte{0x49};
    bytes[2] = std::byte{0x4E};
    bytes[3] = std::byte{0x41};
    bytes[4] = std::byte{0x53};
    bytes[5] = std::byte{0x48};
    bytes[6] = std::byte{0x45};
    bytes[7] = std::byte{0x4C};
    bytes[8] = std::byte{0x4C};
    bytes[15] = std::byte{0x01};
    return *Core::AssetId::fromBytes(bytes);
}

[[nodiscard]] inline constexpr UI::UIIconContent desktopShellIconContent(
    DesktopShellIcon icon) noexcept
{
    const Core::u32 index = static_cast<Core::u32>(icon);
    return UI::UIIconContent{
        .source = {
            .texture = desktopShellIconAtlasAssetId(),
            .sourcePixels = {
                .x = index * DesktopShellIconExtent,
                .y = 0,
                .width = DesktopShellIconExtent,
                .height = DesktopShellIconExtent,
            },
            .texturePixelExtent = {
                .width = DesktopShellIconAtlasWidth,
                .height = DesktopShellIconAtlasHeight,
            },
            .intrinsicLogicalSize = {
                .width = static_cast<float>(DesktopShellIconExtent),
                .height = static_cast<float>(DesktopShellIconExtent),
            },
        },
        .sampling = UI::UIImageSampling::Nearest,
    };
}

using DesktopShellIconAtlasPixels =
    std::array<std::byte, DesktopShellIconAtlasWidth * DesktopShellIconAtlasHeight * 4U>;

constexpr void setDesktopShellIconPixel(DesktopShellIconAtlasPixels& pixels,
                                        Core::u32 iconIndex, Core::u32 x, Core::u32 y,
                                        Core::u8 alpha = 255) noexcept
{
    const Core::u32 atlasX = iconIndex * DesktopShellIconExtent + x;
    const Core::usize offset =
        (static_cast<Core::usize>(y) * DesktopShellIconAtlasWidth + atlasX) * 4U;
    pixels[offset] = std::byte{255};
    pixels[offset + 1U] = std::byte{255};
    pixels[offset + 2U] = std::byte{255};
    pixels[offset + 3U] = static_cast<std::byte>(alpha);
}

[[nodiscard]] inline constexpr DesktopShellIconAtlasPixels
makeDesktopShellIconAtlasPixels() noexcept
{
    DesktopShellIconAtlasPixels pixels{};

    for (Core::u32 y = 3; y <= 12; ++y) {
        for (Core::u32 x = 4; x <= 11; ++x) {
            if (x <= 4U + (y - 3U) / 2U && y <= 10U) {
                setDesktopShellIconPixel(pixels, 0, x, y);
            }
        }
    }

    for (Core::u32 y = 3; y <= 12; ++y) {
        for (Core::u32 x = 3; x <= 12; ++x) {
            const bool outline = x == 3U || x == 12U || y == 3U || y == 12U;
            const bool label = y >= 4U && y <= 7U && x >= 6U && x <= 10U;
            const bool slot = y >= 9U && y <= 11U && x >= 5U && x <= 10U;
            if (outline || label || slot) {
                setDesktopShellIconPixel(pixels, 1, x, y);
            }
        }
    }

    for (Core::u32 x = 3; x <= 10; ++x) {
        setDesktopShellIconPixel(pixels, 2, x, 5);
    }
    for (Core::u32 y = 5; y <= 11; ++y) {
        setDesktopShellIconPixel(pixels, 2, 3, y);
    }
    setDesktopShellIconPixel(pixels, 2, 4, 4);
    setDesktopShellIconPixel(pixels, 2, 5, 3);
    setDesktopShellIconPixel(pixels, 2, 4, 6);
    for (Core::u32 x = 4; x <= 11; ++x) {
        setDesktopShellIconPixel(pixels, 2, x, 11);
    }

    for (Core::u32 x = 5; x <= 12; ++x) {
        setDesktopShellIconPixel(pixels, 3, x, 5);
    }
    for (Core::u32 y = 5; y <= 11; ++y) {
        setDesktopShellIconPixel(pixels, 3, 12, y);
    }
    setDesktopShellIconPixel(pixels, 3, 11, 4);
    setDesktopShellIconPixel(pixels, 3, 10, 3);
    setDesktopShellIconPixel(pixels, 3, 11, 6);
    for (Core::u32 x = 4; x <= 11; ++x) {
        setDesktopShellIconPixel(pixels, 3, x, 11);
    }

    for (Core::u32 x = 4; x <= 11; ++x) {
        setDesktopShellIconPixel(pixels, 4, x, 4);
    }
    for (Core::u32 y = 5; y <= 12; ++y) {
        setDesktopShellIconPixel(pixels, 4, 5, y);
        setDesktopShellIconPixel(pixels, 4, 10, y);
    }
    for (Core::u32 x = 5; x <= 10; ++x) {
        setDesktopShellIconPixel(pixels, 4, x, 12);
    }
    setDesktopShellIconPixel(pixels, 4, 6, 2);
    setDesktopShellIconPixel(pixels, 4, 9, 2);
    for (Core::u32 x = 5; x <= 10; ++x) {
        setDesktopShellIconPixel(pixels, 4, x, 3);
    }

    return pixels;
}

} // namespace Tina::SampleUI
