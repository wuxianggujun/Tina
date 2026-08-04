#include <tina/asset_format/EnvironmentMapPayload.hpp>

#include <tina/asset_format/AssetFormatErrors.hpp>

#include <cstring>
#include <limits>
#include <new>
#include <utility>

namespace Tina::AssetFormat {
namespace {

using Core::u16;
using Core::u32;
using Core::u64;
using Core::u8;
using Core::usize;

struct EnvironmentMapByteLayout final {
    u32 diffuseBytes = 0;
    u32 specularBytes = 0;
    u32 brdfBytes = 0;
    u64 payloadBytes = 0;
};

[[nodiscard]] u8 readU8(std::span<const std::byte> bytes, usize offset) noexcept
{
    return std::to_integer<u8>(bytes[offset]);
}

[[nodiscard]] u16 readU16(std::span<const std::byte> bytes, usize offset) noexcept
{
    return static_cast<u16>(readU8(bytes, offset)) |
           static_cast<u16>(static_cast<u16>(readU8(bytes, offset + 1U)) << 8U);
}

[[nodiscard]] u32 readU32(std::span<const std::byte> bytes, usize offset) noexcept
{
    u32 value = 0;
    for (usize index = 0; index < 4U; ++index)
    {
        value |= static_cast<u32>(readU8(bytes, offset + index)) << (index * 8U);
    }
    return value;
}

void writeU8(std::vector<std::byte>& bytes, usize offset, u8 value)
{
    bytes.at(offset) = static_cast<std::byte>(value);
}

void writeU16(std::vector<std::byte>& bytes, usize offset, u16 value)
{
    writeU8(bytes, offset, static_cast<u8>(value & 0xFFU));
    writeU8(bytes, offset + 1U, static_cast<u8>((value >> 8U) & 0xFFU));
}

void writeU32(std::vector<std::byte>& bytes, usize offset, u32 value)
{
    for (usize index = 0; index < 4U; ++index)
    {
        writeU8(bytes, offset + index, static_cast<u8>((value >> (index * 8U)) & 0xFFU));
    }
}

[[nodiscard]] Core::Result<EnvironmentMapByteLayout> calculateByteLayout(u16 diffuseFaceSize,
                                                                          u16 specularFaceSize,
                                                                          u16 specularMipCount,
                                                                          u16 brdfWidth,
                                                                          u16 brdfHeight)
{
    if (diffuseFaceSize == 0 || specularFaceSize == 0 || brdfWidth == 0 || brdfHeight == 0)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "environment map dimensions must be non-zero");
    }
    if (specularMipCount != EnvironmentMapWire::fullMipCount(specularFaceSize))
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "environment map specular mip count must describe the complete chain");
    }

    constexpr u64 MaxU32 = (std::numeric_limits<u32>::max)();
    const auto diffuseBytes = static_cast<u64>(diffuseFaceSize) * diffuseFaceSize * EnvironmentMapWire::FaceCount *
                              EnvironmentMapWire::Rgba16FloatBytesPerPixel;

    u64 specularBytes = 0;
    u16 mipFaceSize = specularFaceSize;
    for (u16 mip = 0; mip < specularMipCount; ++mip)
    {
        specularBytes += static_cast<u64>(mipFaceSize) * mipFaceSize * EnvironmentMapWire::FaceCount *
                         EnvironmentMapWire::Rgba16FloatBytesPerPixel;
        mipFaceSize = static_cast<u16>(mipFaceSize / 2U);
    }

    const auto brdfBytes = static_cast<u64>(brdfWidth) * brdfHeight * EnvironmentMapWire::Rg16FloatBytesPerPixel;
    if (diffuseBytes > MaxU32 || specularBytes > MaxU32 || brdfBytes > MaxU32)
    {
        return Core::failure(AssetFormatErrorCode::ArithmeticOverflow,
                             "environment map image byte count exceeds wire field capacity");
    }

    const auto payloadBytes = static_cast<u64>(EnvironmentMapWire::HeaderBytes) + diffuseBytes + specularBytes +
                              brdfBytes;
    if (payloadBytes > Wire::MaxPayloadBytes)
    {
        return Core::failure(AssetFormatErrorCode::SizeLimitExceeded,
                             "environment map payload exceeds cooked payload limit");
    }

    return EnvironmentMapByteLayout{
        .diffuseBytes = static_cast<u32>(diffuseBytes),
        .specularBytes = static_cast<u32>(specularBytes),
        .brdfBytes = static_cast<u32>(brdfBytes),
        .payloadBytes = payloadBytes,
    };
}

} // namespace

Core::Result<std::vector<std::byte>> writeEnvironmentMapPayloadBytes(const EnvironmentMapPayloadDesc& desc)
{
    if (desc.radiancePixelFormat != EnvironmentMapRadiancePixelFormat::Rgba16Float)
    {
        return Core::failure(AssetFormatErrorCode::UnsupportedValue,
                             "unsupported environment map radiance pixel format");
    }
    if (desc.brdfPixelFormat != EnvironmentMapBrdfPixelFormat::Rg16Float)
    {
        return Core::failure(AssetFormatErrorCode::UnsupportedValue, "unsupported environment map BRDF pixel format");
    }

    auto layout = calculateByteLayout(desc.diffuseFaceSize, desc.specularFaceSize, desc.specularMipCount,
                                      desc.brdfWidth, desc.brdfHeight);
    if (!layout)
    {
        return Core::failure(std::move(layout.error()));
    }
    if (desc.diffusePixels.size() != layout->diffuseBytes || desc.specularPixels.size() != layout->specularBytes ||
        desc.brdfPixels.size() != layout->brdfBytes)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "environment map image buffer size does not match declared dimensions");
    }

    try
    {
        std::vector<std::byte> bytes(static_cast<usize>(layout->payloadBytes), std::byte{0});
        writeU16(bytes, 0U, EnvironmentMapWire::SchemaVersion);
        writeU16(bytes, 2U, static_cast<u16>(desc.radiancePixelFormat));
        writeU16(bytes, 4U, static_cast<u16>(desc.brdfPixelFormat));
        writeU16(bytes, 6U, desc.diffuseFaceSize);
        writeU16(bytes, 8U, desc.specularFaceSize);
        writeU16(bytes, 10U, desc.specularMipCount);
        writeU16(bytes, 12U, desc.brdfWidth);
        writeU16(bytes, 14U, desc.brdfHeight);
        writeU32(bytes, 16U, layout->diffuseBytes);
        writeU32(bytes, 20U, layout->specularBytes);
        writeU32(bytes, 24U, layout->brdfBytes);
        writeU32(bytes, 28U, 0U);

        usize offset = EnvironmentMapWire::HeaderBytes;
        std::memcpy(bytes.data() + offset, desc.diffusePixels.data(), layout->diffuseBytes);
        offset += layout->diffuseBytes;
        std::memcpy(bytes.data() + offset, desc.specularPixels.data(), layout->specularBytes);
        offset += layout->specularBytes;
        std::memcpy(bytes.data() + offset, desc.brdfPixels.data(), layout->brdfBytes);
        return bytes;
    } catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory, "environment map payload allocation failed");
    }
}

Core::Result<EnvironmentMapPayloadView> parseEnvironmentMapPayload(std::span<const std::byte> payload)
{
    if (payload.size() < EnvironmentMapWire::HeaderBytes)
    {
        return Core::failure(AssetFormatErrorCode::InvalidHeader, "environment map payload too small");
    }

    EnvironmentMapPayloadView view{
        .schemaVersion = readU16(payload, 0U),
        .radiancePixelFormat = static_cast<EnvironmentMapRadiancePixelFormat>(readU16(payload, 2U)),
        .brdfPixelFormat = static_cast<EnvironmentMapBrdfPixelFormat>(readU16(payload, 4U)),
        .diffuseFaceSize = readU16(payload, 6U),
        .specularFaceSize = readU16(payload, 8U),
        .specularMipCount = readU16(payload, 10U),
        .brdfWidth = readU16(payload, 12U),
        .brdfHeight = readU16(payload, 14U),
        .diffuseBytes = readU32(payload, 16U),
        .specularBytes = readU32(payload, 20U),
        .brdfBytes = readU32(payload, 24U),
    };

    if (view.schemaVersion != EnvironmentMapWire::SchemaVersion)
    {
        return Core::failure(AssetFormatErrorCode::UnsupportedSchema,
                             "unsupported environment map payload schema");
    }
    if (view.radiancePixelFormat != EnvironmentMapRadiancePixelFormat::Rgba16Float ||
        view.brdfPixelFormat != EnvironmentMapBrdfPixelFormat::Rg16Float)
    {
        return Core::failure(AssetFormatErrorCode::UnsupportedValue,
                             "unsupported environment map payload pixel format");
    }
    if (readU32(payload, 28U) != 0U)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "environment map payload reserved field must be zero");
    }

    auto layout = calculateByteLayout(view.diffuseFaceSize, view.specularFaceSize, view.specularMipCount,
                                      view.brdfWidth, view.brdfHeight);
    if (!layout)
    {
        return Core::failure(std::move(layout.error()));
    }
    if (view.diffuseBytes != layout->diffuseBytes || view.specularBytes != layout->specularBytes ||
        view.brdfBytes != layout->brdfBytes || payload.size() != layout->payloadBytes)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "environment map payload byte counts do not match dimensions");
    }

    usize offset = EnvironmentMapWire::HeaderBytes;
    view.diffusePixels = payload.subspan(offset, view.diffuseBytes);
    offset += view.diffuseBytes;
    view.specularPixels = payload.subspan(offset, view.specularBytes);
    offset += view.specularBytes;
    view.brdfPixels = payload.subspan(offset, view.brdfBytes);
    return view;
}

Core::Result<std::vector<std::byte>> writeCookedEnvironmentMapAsset(Core::AssetId assetId,
                                                                    const EnvironmentMapPayloadDesc& desc,
                                                                    TargetPlatform platform)
{
    auto payload = writeEnvironmentMapPayloadBytes(desc);
    if (!payload)
    {
        return Core::failure(std::move(payload.error()));
    }
    return writeCookedAssetBytes(CookedAssetWriteDesc{
        .assetKind = AssetKind::EnvironmentMap,
        .assetTypeVersion = EnvironmentMapWire::SchemaVersion,
        .targetPlatform = platform,
        .assetId = assetId,
        .payload = *payload,
        .payloadAlignment = 16,
        .computeContentHash = true,
    });
}

} // namespace Tina::AssetFormat
