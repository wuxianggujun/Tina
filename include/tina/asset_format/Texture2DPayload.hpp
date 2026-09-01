#pragma once

#include <tina/asset_format/AssetFormat.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>

#include <array>
#include <span>
#include <vector>

namespace Tina::AssetFormat {

// Texture2D cooked payload schema v2 (little-endian, after CookedAsset header/deps).
//
// v1 carried a single uncompressed Rgba8Unorm level with no colour space and no
// sampler, so a cooked texture could not state how it must be filtered or whether
// its bytes were already gamma encoded. Both are properties of the asset rather than
// of the draw call, so they belong in the payload. v1 is deleted rather than kept
// alongside: a v1 file is rejected with UnsupportedSchema.
//
// Layout (32B header + level descriptors + level bytes):
//   u16 schemaVersion (=2)
//   u16 width                 // level 0
//   u16 height                // level 0
//   u16 pixelFormat
//   u8  colorSpace
//   u8  levelCount            // >= 1; a complete chain to 1x1 when > 1
//   u8  wrapU
//   u8  wrapV
//   u8  minFilter
//   u8  magFilter
//   u8  mipFilter
//   u8  reserved0 (=0)
//   u32 levelBytes            // total bytes across every level
//   u32 reserved1 (=0)
//   u32 reserved2 (=0)
//   u32 reserved3 (=0)
//   { u32 byteOffset; u32 byteSize; } levels[levelCount]  // offsets from payload start
//   u8 levelData[levelBytes]
namespace Texture2DWire {
inline constexpr Core::u16 SchemaVersion = 2;
inline constexpr Core::u32 HeaderBytes = 32;
inline constexpr Core::u32 LevelDescriptorBytes = 8;
inline constexpr Core::u32 MaxDimension = 16384;
// 16384 needs exactly 15 levels to reach 1x1, so this is the chain length rather
// than a round number.
inline constexpr Core::u8 MaxLevelCount = 15;
} // namespace Texture2DWire

enum class Texture2DPixelFormat : Core::u16 {
    Invalid = 0,
    Rgba8Unorm = 1,
    // Block-compressed formats, stored as cooked. The runtime uploads them as-is;
    // the cooker is the only place allowed to transcode, and a backend without
    // support rejects rather than silently decompressing on the CPU.
    Bc1Rgba = 2,     // 8 bytes per 4x4 block
    Bc3Rgba = 3,     // 16 bytes per 4x4 block
    Bc7Rgba = 4,     // 16 bytes per 4x4 block
    Astc4x4Rgba = 5, // 16 bytes per 4x4 block
};

// Whether the stored bytes are gamma encoded. Not cosmetic: sampling an sRGB
// texture as linear double-applies gamma, which reads as a washed-out asset rather
// than as a bug.
enum class Texture2DColorSpace : Core::u8 {
    Invalid = 0,
    Linear = 1,
    Srgb = 2,
};

enum class Texture2DWrapMode : Core::u8 {
    Invalid = 0,
    Repeat = 1,
    Mirror = 2,
    Clamp = 3,
    Border = 4,
};

enum class Texture2DFilterMode : Core::u8 {
    Invalid = 0,
    Point = 1,
    Linear = 2,
    Anisotropic = 3,
};

// Mip selection is separate from min/mag: a texture may want linear in-level
// filtering with point mip selection, which is the sharp-mip look.
enum class Texture2DMipFilterMode : Core::u8 {
    // Valid only for a single-level texture. A complete mip chain must select Point
    // or Linear because bgfx cannot upload levels while disabling mip selection.
    None = 0,
    Point = 1,
    Linear = 2,
};

struct Texture2DSamplerDesc final {
    Texture2DWrapMode wrapU = Texture2DWrapMode::Repeat;
    Texture2DWrapMode wrapV = Texture2DWrapMode::Repeat;
    Texture2DFilterMode minFilter = Texture2DFilterMode::Linear;
    Texture2DFilterMode magFilter = Texture2DFilterMode::Linear;
    Texture2DMipFilterMode mipFilter = Texture2DMipFilterMode::Linear;

    friend constexpr bool operator==(const Texture2DSamplerDesc&,
                                     const Texture2DSamplerDesc&) noexcept = default;
};

// One mip level's bytes. The cooker supplies the whole chain; the runtime never
// generates mips, because generating them at load time would make one asset produce
// different pixels depending on which backend loaded it.
struct Texture2DLevelDesc final {
    Core::u16 width = 0;
    Core::u16 height = 0;
    std::span<const std::byte> bytes{};
};

struct Texture2DPayloadDesc final {
    Texture2DPixelFormat pixelFormat = Texture2DPixelFormat::Rgba8Unorm;
    Texture2DColorSpace colorSpace = Texture2DColorSpace::Srgb;
    Texture2DSamplerDesc sampler{};
    // Level 0 first, each level half the previous extent (rounded down, floored at
    // 1). A single level means no mip chain.
    std::span<const Texture2DLevelDesc> levels{};
};

struct Texture2DPayloadLevelView final {
    Core::u16 width = 0;
    Core::u16 height = 0;
    std::span<const std::byte> bytes{};
};

struct Texture2DPayloadView final {
    Core::u16 schemaVersion = 0;
    Core::u16 width = 0;
    Core::u16 height = 0;
    Texture2DPixelFormat pixelFormat = Texture2DPixelFormat::Invalid;
    Texture2DColorSpace colorSpace = Texture2DColorSpace::Invalid;
    Texture2DSamplerDesc sampler{};
    Core::u8 levelCount = 0;
    Core::u32 levelBytes = 0;

    // The level table is stored inline rather than in caller-supplied storage: the wire
    // format caps a chain at 15 levels, so the bound is known at compile time, and an
    // inline table removes both an allocation and the chance of a view outliving the
    // storage its levels point into. The per-level byte spans still borrow the payload.
    std::array<Texture2DPayloadLevelView, Texture2DWire::MaxLevelCount> levelTable{};

    // Index 0 is the base level.
    [[nodiscard]] std::span<const Texture2DPayloadLevelView> levels() const noexcept
    {
        return std::span<const Texture2DPayloadLevelView>{levelTable}.first(levelCount);
    }

    // Base level bytes, which is what a single-level consumer wants.
    [[nodiscard]] std::span<const std::byte> basePixels() const noexcept
    {
        return levelCount == 0 ? std::span<const std::byte>{} : levelTable.front().bytes;
    }
};

// Bytes one level occupies for a format and extent, accounting for 4x4 block
// compression. Zero means the extent or format is invalid.
[[nodiscard]] Core::u32 texture2DLevelByteSize(Texture2DPixelFormat format, Core::u16 width,
                                               Core::u16 height) noexcept;

[[nodiscard]] bool isBlockCompressedTexture2DFormat(Texture2DPixelFormat format) noexcept;

// Levels in a complete chain from the given base extent down to 1x1.
[[nodiscard]] Core::u8 texture2DFullMipLevelCount(Core::u16 width, Core::u16 height) noexcept;

[[nodiscard]] Core::Status validateTexture2DSamplerDesc(const Texture2DSamplerDesc& sampler) noexcept;

[[nodiscard]] Core::Result<std::vector<std::byte>> writeTexture2DPayloadBytes(const Texture2DPayloadDesc& desc);

// Single uncompressed level with no mip chain. Most producers and fixtures want exactly
// this, and spelling out a one-element level span for a 1x1 texture obscures what the
// call is about.
[[nodiscard]] Core::Result<std::vector<std::byte>>
writeTexture2DPayloadBytesRgba8(Core::u16 width, Core::u16 height,
                                std::span<const std::byte> rgba8Pixels,
                                Texture2DColorSpace colorSpace = Texture2DColorSpace::Srgb);

// Borrows payload bytes from a CookedAssetView / raw payload span. The returned view
// carries its own level table, so only the payload bytes must outlive it.
[[nodiscard]] Core::Result<Texture2DPayloadView>
parseTexture2DPayload(std::span<const std::byte> payload);

// Convenience: full cooked Texture2D asset file (no dependencies).
[[nodiscard]] Core::Result<std::vector<std::byte>>
writeCookedTexture2DAsset(Core::AssetId assetId, const Texture2DPayloadDesc& desc,
                          TargetPlatform platform = TargetPlatform::WindowsX64);

// Single uncompressed level with default sampler. Most call sites want exactly this,
// and spelling out a level span for a 1x1 fixture obscures what the test is about.
[[nodiscard]] Core::Result<std::vector<std::byte>>
writeCookedTexture2DAssetRgba8(Core::AssetId assetId, Core::u16 width, Core::u16 height,
                               std::span<const std::byte> rgba8Pixels,
                               Texture2DColorSpace colorSpace = Texture2DColorSpace::Srgb,
                               TargetPlatform platform = TargetPlatform::WindowsX64);

} // namespace Tina::AssetFormat
