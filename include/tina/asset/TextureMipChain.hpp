#pragma once

#include <tina/asset_format/Texture2DPayload.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>

#include <array>
#include <span>
#include <vector>

namespace Tina::Asset {

// Cook-time mip chain generation for uncompressed RGBA8 pixels.
//
// This lives on the cook side because a mip chain is asset content: generating it at
// load time would make one cooked asset produce different pixels depending on which
// backend loaded it, which is why Texture2DPayload states the runtime never does it.
// Compressed formats are not accepted -- transcoding is a separate step that must run
// after downsampling, since compressing first and then averaging block colours
// compounds the block error at every level.
struct Texture2DMipChainRgba8 final {
    struct Level final {
        Core::u16 width = 0;
        Core::u16 height = 0;
        Core::u32 byteOffset = 0;
        Core::u32 byteSize = 0;
    };

    // Every level concatenated, base level first.
    std::vector<std::byte> bytes{};
    // Levels are held as offsets rather than spans so that moving the chain cannot
    // leave a level pointing at a buffer the move already took.
    std::array<Level, AssetFormat::Texture2DWire::MaxLevelCount> levels{};
    Core::u8 levelCount = 0;

    // Fills caller-owned storage with descriptors borrowing this chain's bytes, and
    // returns the populated prefix ready to hand to writeTexture2DPayloadBytes. The
    // storage must outlive the returned span, and so must this chain.
    [[nodiscard]] std::span<const AssetFormat::Texture2DLevelDesc> fillLevelDescs(
        std::array<AssetFormat::Texture2DLevelDesc, AssetFormat::Texture2DWire::MaxLevelCount>&
            storage) const noexcept;
};

// Builds a complete chain from the base extent down to 1x1 by 2x2 box filtering.
//
// Downsampling is colour-space aware: sRGB bytes are gamma encoded, so averaging them
// directly yields mips that are visibly too dark. They are decoded to linear, averaged,
// and re-encoded through the exact sRGB transfer function -- the same one the GPU's sRGB
// sampler uses, because an approximation would make a mip disagree with its base level
// about what a stored byte means.
//
// Averaging is alpha weighted for the same class of reason: the RGB under a fully
// transparent pixel is arbitrary, and a straight mean drags it into the visible result
// as a halo. Where a destination pixel's coverage is entirely transparent its RGB is
// left at zero rather than divided by zero alpha.
//
// A single-pixel base yields a one-level chain. Fails when the extent is out of range
// for the wire format or the pixel span does not match the base extent.
[[nodiscard]] Core::Result<Texture2DMipChainRgba8>
buildTexture2DMipChainRgba8(Core::u16 width, Core::u16 height,
                            std::span<const std::byte> rgba8BasePixels,
                            AssetFormat::Texture2DColorSpace colorSpace);

} // namespace Tina::Asset
