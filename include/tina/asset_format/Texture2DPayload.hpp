#pragma once

#include <tina/asset_format/AssetFormat.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>

#include <span>
#include <vector>

namespace Tina::AssetFormat {

// Texture2D cooked payload schema v1 (little-endian, after CookedAsset header/deps).
// Layout (16B header + pixels):
//   u16 schemaVersion (=1)
//   u16 width
//   u16 height
//   u16 pixelFormat   (1 = Rgba8Unorm)
//   u32 pixelBytes
//   u32 reserved (=0)
//   u8  pixels[pixelBytes]   // row-major, width*height*4 for Rgba8Unorm
namespace Texture2DWire {
inline constexpr Core::u16 SchemaVersion = 1;
inline constexpr Core::u32 HeaderBytes = 16;
inline constexpr Core::u32 MaxDimension = 16384;
} // namespace Texture2DWire

enum class Texture2DPixelFormat : Core::u16 {
    Invalid = 0,
    Rgba8Unorm = 1,
};

struct Texture2DPayloadDesc final {
    Core::u16 width = 0;
    Core::u16 height = 0;
    Texture2DPixelFormat pixelFormat = Texture2DPixelFormat::Rgba8Unorm;
    std::span<const std::byte> pixels{};
};

struct Texture2DPayloadView final {
    Core::u16 schemaVersion = 0;
    Core::u16 width = 0;
    Core::u16 height = 0;
    Texture2DPixelFormat pixelFormat = Texture2DPixelFormat::Invalid;
    Core::u32 pixelBytes = 0;
    std::span<const std::byte> pixels{};
};

[[nodiscard]] Core::Result<std::vector<std::byte>> writeTexture2DPayloadBytes(const Texture2DPayloadDesc& desc);

// Borrows payload bytes from a CookedAssetView / raw payload span.
[[nodiscard]] Core::Result<Texture2DPayloadView> parseTexture2DPayload(std::span<const std::byte> payload);

// Convenience: full cooked Texture2D asset file (no dependencies).
[[nodiscard]] Core::Result<std::vector<std::byte>>
writeCookedTexture2DAsset(Core::AssetId assetId, const Texture2DPayloadDesc& desc,
                          TargetPlatform platform = TargetPlatform::WindowsX64);

} // namespace Tina::AssetFormat
