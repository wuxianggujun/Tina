#include <tina/asset_format/Texture2DPayload.hpp>

#include <tina/asset_format/AssetFormatErrors.hpp>

#include <cstring>
#include <limits>

namespace Tina::AssetFormat {
namespace {

using Core::u16;
using Core::u32;
using Core::u8;
using Core::usize;

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

[[nodiscard]] bool checkedMultiply(u32 a, u32 b, u32& out) noexcept
{
    const auto wide = static_cast<std::uint64_t>(a) * static_cast<std::uint64_t>(b);
    if (wide > (std::numeric_limits<u32>::max)())
    {
        return false;
    }
    out = static_cast<u32>(wide);
    return true;
}

} // namespace

Core::Result<std::vector<std::byte>> writeTexture2DPayloadBytes(const Texture2DPayloadDesc& desc)
{
    if (desc.width == 0 || desc.height == 0 || desc.width > Texture2DWire::MaxDimension ||
        desc.height > Texture2DWire::MaxDimension)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "texture dimensions out of range");
    }
    if (desc.pixelFormat != Texture2DPixelFormat::Rgba8Unorm)
    {
        return Core::failure(AssetFormatErrorCode::UnsupportedValue, "unsupported texture pixel format");
    }
    u32 expectedPixels = 0;
    if (!checkedMultiply(desc.width, desc.height, expectedPixels) ||
        !checkedMultiply(expectedPixels, 4U, expectedPixels))
    {
        return Core::failure(AssetFormatErrorCode::ArithmeticOverflow, "texture pixel size overflow");
    }
    if (desc.pixels.size() != expectedPixels)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "texture pixel buffer size mismatch");
    }

    try
    {
        std::vector<std::byte> bytes(Texture2DWire::HeaderBytes + expectedPixels, std::byte{0});
        writeU16(bytes, 0U, Texture2DWire::SchemaVersion);
        writeU16(bytes, 2U, desc.width);
        writeU16(bytes, 4U, desc.height);
        writeU16(bytes, 6U, static_cast<u16>(desc.pixelFormat));
        writeU32(bytes, 8U, expectedPixels);
        writeU32(bytes, 12U, 0U);
        std::memcpy(bytes.data() + Texture2DWire::HeaderBytes, desc.pixels.data(), expectedPixels);
        return bytes;
    } catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory, "texture payload allocation failed");
    }
}

Core::Result<Texture2DPayloadView> parseTexture2DPayload(std::span<const std::byte> payload)
{
    if (payload.size() < Texture2DWire::HeaderBytes)
    {
        return Core::failure(AssetFormatErrorCode::InvalidHeader, "texture payload too small");
    }
    Texture2DPayloadView view{};
    view.schemaVersion = readU16(payload, 0U);
    view.width = readU16(payload, 2U);
    view.height = readU16(payload, 4U);
    view.pixelFormat = static_cast<Texture2DPixelFormat>(readU16(payload, 6U));
    view.pixelBytes = readU32(payload, 8U);
    const auto reserved = readU32(payload, 12U);
    if (view.schemaVersion != Texture2DWire::SchemaVersion)
    {
        return Core::failure(AssetFormatErrorCode::UnsupportedSchema, "unsupported texture payload schema");
    }
    if (reserved != 0U)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "texture payload reserved must be zero");
    }
    if (view.width == 0 || view.height == 0 || view.width > Texture2DWire::MaxDimension ||
        view.height > Texture2DWire::MaxDimension)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "texture dimensions invalid");
    }
    if (view.pixelFormat != Texture2DPixelFormat::Rgba8Unorm)
    {
        return Core::failure(AssetFormatErrorCode::UnsupportedValue, "unsupported texture pixel format");
    }
    u32 expected = 0;
    if (!checkedMultiply(view.width, view.height, expected) || !checkedMultiply(expected, 4U, expected))
    {
        return Core::failure(AssetFormatErrorCode::ArithmeticOverflow, "texture pixel size overflow");
    }
    if (view.pixelBytes != expected || payload.size() != Texture2DWire::HeaderBytes + view.pixelBytes)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "texture pixel bytes mismatch");
    }
    view.pixels = payload.subspan(Texture2DWire::HeaderBytes, view.pixelBytes);
    return view;
}

Core::Result<std::vector<std::byte>> writeCookedTexture2DAsset(Core::AssetId assetId, const Texture2DPayloadDesc& desc,
                                                               TargetPlatform platform)
{
    auto payload = writeTexture2DPayloadBytes(desc);
    if (!payload)
    {
        return Core::failure(std::move(payload.error()));
    }
    return writeCookedAssetBytes(CookedAssetWriteDesc{
        .assetKind = AssetKind::Texture2D,
        .assetTypeVersion = Texture2DWire::SchemaVersion,
        .targetPlatform = platform,
        .assetId = assetId,
        .payload = *payload,
        .payloadAlignment = 16,
        .computeContentHash = true,
    });
}

} // namespace Tina::AssetFormat
