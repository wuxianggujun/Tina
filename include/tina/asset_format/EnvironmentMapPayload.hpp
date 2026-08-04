#pragma once

#include <tina/asset_format/AssetFormat.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>

#include <span>
#include <vector>

namespace Tina::AssetFormat {

// EnvironmentMap cooked payload schema v1. All image data is prefiltered offline;
// runtime consumers upload these bytes directly and never perform convolution.
//
// Layout (32B little-endian header + image data):
//   u16 schemaVersion (=1)
//   u16 radiancePixelFormat (1 = Rgba16Float)
//   u16 brdfPixelFormat (1 = Rg16Float)
//   u16 diffuseFaceSize
//   u16 specularFaceSize
//   u16 specularMipCount
//   u16 brdfWidth
//   u16 brdfHeight
//   u32 diffuseBytes
//   u32 specularBytes
//   u32 brdfBytes
//   u32 reserved (=0)
//   u8 diffuse[diffuseBytes] // six RGBA16F faces: +X, -X, +Y, -Y, +Z, -Z
//   u8 specular[specularBytes] // mip-major; each mip uses the same face order
//   u8 brdf[brdfBytes] // row-major RG16F BRDF integration LUT
namespace EnvironmentMapWire {
inline constexpr Core::u16 SchemaVersion = 1;
inline constexpr Core::u32 HeaderBytes = 32;
inline constexpr Core::u16 FaceCount = 6;
inline constexpr Core::u16 Rgba16FloatBytesPerPixel = 8;
inline constexpr Core::u16 Rg16FloatBytesPerPixel = 4;

[[nodiscard]] constexpr Core::u16 fullMipCount(Core::u16 faceSize) noexcept
{
    Core::u16 count = 0;
    while (faceSize != 0)
    {
        ++count;
        faceSize = static_cast<Core::u16>(faceSize / 2U);
    }
    return count;
}
} // namespace EnvironmentMapWire

enum class EnvironmentMapRadiancePixelFormat : Core::u16 {
    Invalid = 0,
    Rgba16Float = 1,
};

enum class EnvironmentMapBrdfPixelFormat : Core::u16 {
    Invalid = 0,
    Rg16Float = 1,
};

struct EnvironmentMapPayloadDesc final {
    EnvironmentMapRadiancePixelFormat radiancePixelFormat = EnvironmentMapRadiancePixelFormat::Rgba16Float;
    EnvironmentMapBrdfPixelFormat brdfPixelFormat = EnvironmentMapBrdfPixelFormat::Rg16Float;
    Core::u16 diffuseFaceSize = 0;
    Core::u16 specularFaceSize = 0;
    Core::u16 specularMipCount = 0;
    Core::u16 brdfWidth = 0;
    Core::u16 brdfHeight = 0;
    std::span<const std::byte> diffusePixels{};
    std::span<const std::byte> specularPixels{};
    std::span<const std::byte> brdfPixels{};
};

struct EnvironmentMapPayloadView final {
    Core::u16 schemaVersion = 0;
    EnvironmentMapRadiancePixelFormat radiancePixelFormat = EnvironmentMapRadiancePixelFormat::Invalid;
    EnvironmentMapBrdfPixelFormat brdfPixelFormat = EnvironmentMapBrdfPixelFormat::Invalid;
    Core::u16 diffuseFaceSize = 0;
    Core::u16 specularFaceSize = 0;
    Core::u16 specularMipCount = 0;
    Core::u16 brdfWidth = 0;
    Core::u16 brdfHeight = 0;
    Core::u32 diffuseBytes = 0;
    Core::u32 specularBytes = 0;
    Core::u32 brdfBytes = 0;
    std::span<const std::byte> diffusePixels{};
    std::span<const std::byte> specularPixels{};
    std::span<const std::byte> brdfPixels{};
};

[[nodiscard]] Core::Result<std::vector<std::byte>>
writeEnvironmentMapPayloadBytes(const EnvironmentMapPayloadDesc& desc);

// Borrows all image spans from the supplied payload bytes.
[[nodiscard]] Core::Result<EnvironmentMapPayloadView>
parseEnvironmentMapPayload(std::span<const std::byte> payload);

// Convenience: full cooked EnvironmentMap asset file with no dependencies.
[[nodiscard]] Core::Result<std::vector<std::byte>>
writeCookedEnvironmentMapAsset(Core::AssetId assetId, const EnvironmentMapPayloadDesc& desc,
                               TargetPlatform platform = TargetPlatform::WindowsX64);

} // namespace Tina::AssetFormat
