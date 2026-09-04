#pragma once

#include <tina/asset_format/AssetFormat.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>

#include <array>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace Tina::AssetFormat {

// Shader cooked payload schema v1. Carries one compiled binary per renderer profile for a
// single pipeline stage; the payload is opaque to this module, which only validates the table.
//
// Layout (16B little-endian header + blob table + blob bytes):
//   u16 schemaVersion (=1)
//   u16 shaderKind    (ShaderKind)
//   u16 stage         (ShaderStage)
//   u16 blobCount     ([1, MaxBlobCount])
//   u32 blobBytesTotal (sum of every entry byteCount)
//   u32 reserved (=0)
//   BlobEntry[blobCount], each 12B:
//     u16 profile (ShaderBinaryProfile)
//     u16 reserved (=0)
//     u32 byteOffset (from the start of the payload)
//     u32 byteCount  (>= 1)
//   u8 blobBytes[blobBytesTotal]
//
// Entries are strictly ascending by profile and their bytes are tightly packed in that order,
// so a given set of binaries has exactly one encoding and the content hash is stable across
// cooks. Blobs carry no alignment padding: a consumer hands the span to the render device,
// which copies it before the backend reads words out of it.
namespace ShaderWire {
inline constexpr Core::u16 SchemaVersion = 1;
inline constexpr Core::u32 HeaderBytes = 16;
inline constexpr Core::u32 BlobEntryBytes = 12;
inline constexpr Core::u16 MaxBlobCount = 8;
inline constexpr Core::u32 MaxBlobBytes = 4U * 1024U * 1024U;
inline constexpr Core::u32 MaxPayloadBytes = 16U * 1024U * 1024U;
} // namespace ShaderWire

// The engine program a cooked shader plugs into. It selects the varying contract and the
// engine-owned stage it is linked against, so a Sprite2D binary bound to a Mesh3D draw fails
// closed instead of linking against varyings its source never declared.
enum class ShaderKind : Core::u16 {
    Invalid = 0,
    Sprite2D = 1,
    Mesh3D = 2,
};

// Fragment is the only stage a cooked shader may replace. The vertex stage stays engine-owned
// because it produces the varying contract, the vertex layout and the batching assumptions.
enum class ShaderStage : Core::u16 {
    Invalid = 0,
    Fragment = 1,
};

// Renderer binary flavours, matching the profile set the engine's own shaders are built for
// (see cmake/TinaBgfxEmbeddedShaders.cmake). Picking among them needs the live renderer type,
// so the choice belongs to the backend, not to this module.
enum class ShaderBinaryProfile : Core::u16 {
    Invalid = 0,
    Glsl120 = 1,
    SpirV = 2,
    Dxbc50 = 3,
    Essl300 = 4,
    // Metal Shading Language, the only renderer bgfx selects on modern iOS. Appended rather than
    // ordered beside the other mobile profile because the numeric value is the payload's sort key:
    // blobs are stored strictly ascending, so inserting in the middle would renumber Essl300 and
    // make every already-cooked payload parse as a different profile set.
    Metal = 5,
};

// Recipe-facing spelling: "glsl120", "spv", "dxbc", "essl300", "mtl". Empty for Invalid.
[[nodiscard]] std::string_view shaderBinaryProfileName(ShaderBinaryProfile profile) noexcept;

[[nodiscard]] std::optional<ShaderBinaryProfile>
parseShaderBinaryProfileName(std::string_view name) noexcept;

struct ShaderBlobDesc final {
    ShaderBinaryProfile profile = ShaderBinaryProfile::Invalid;
    std::span<const std::byte> bytes{};
};

struct ShaderPayloadDesc final {
    ShaderKind shaderKind = ShaderKind::Invalid;
    ShaderStage stage = ShaderStage::Fragment;
    // Must already be sorted by strictly ascending profile.
    std::span<const ShaderBlobDesc> blobs{};
};

struct ShaderBlobView final {
    ShaderBinaryProfile profile = ShaderBinaryProfile::Invalid;
    std::span<const std::byte> bytes{};
};

struct ShaderPayloadView final {
    Core::u16 schemaVersion = 0;
    ShaderKind shaderKind = ShaderKind::Invalid;
    ShaderStage stage = ShaderStage::Invalid;
    Core::u16 blobCount = 0;
    std::array<ShaderBlobView, ShaderWire::MaxBlobCount> blobStorage{};

    [[nodiscard]] std::span<const ShaderBlobView> blobs() const noexcept
    {
        return {blobStorage.data(), blobCount};
    }

    // Empty when the profile is absent: every stored blob holds at least one byte.
    [[nodiscard]] std::span<const std::byte> blobForProfile(ShaderBinaryProfile profile) const noexcept;
};

[[nodiscard]] Core::Result<std::vector<std::byte>>
writeShaderPayloadBytes(const ShaderPayloadDesc& desc);

// Borrows every blob span from the supplied payload bytes.
[[nodiscard]] Core::Result<ShaderPayloadView> parseShaderPayload(std::span<const std::byte> payload);

// Convenience: full cooked Shader asset file with no dependencies.
[[nodiscard]] Core::Result<std::vector<std::byte>>
writeCookedShaderAsset(Core::AssetId assetId, const ShaderPayloadDesc& desc,
                       TargetPlatform platform = TargetPlatform::WindowsX64);

} // namespace Tina::AssetFormat
