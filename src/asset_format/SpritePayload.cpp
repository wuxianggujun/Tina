#include <tina/asset_format/SpritePayload.hpp>

#include <tina/asset_format/AssetFormatErrors.hpp>

#include <cmath>
#include <cstring>

namespace Tina::AssetFormat {
namespace {

using Core::u16;
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

[[nodiscard]] float readF32(std::span<const std::byte> bytes, usize offset) noexcept
{
    float value = 0.0f;
    std::memcpy(&value, bytes.data() + offset, sizeof(float));
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

void writeF32(std::vector<std::byte>& bytes, usize offset, float value)
{
    std::memcpy(bytes.data() + offset, &value, sizeof(float));
}

[[nodiscard]] bool isFiniteUv(float value) noexcept
{
    return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
}

} // namespace

Core::Result<std::vector<std::byte>> writeSpritePayloadBytes(const SpritePayloadDesc& desc)
{
    if (!isFiniteUv(desc.u0) || !isFiniteUv(desc.v0) || !isFiniteUv(desc.u1) || !isFiniteUv(desc.v1))
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "sprite UV must be finite in [0,1]");
    }
    if (!(desc.u0 < desc.u1) || !(desc.v0 < desc.v1))
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "sprite UV rect must be non-empty");
    }
    if (!(desc.pixelsPerUnit > 0.0f) || !std::isfinite(desc.pixelsPerUnit))
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "pixelsPerUnit must be positive finite");
    }
    if (!std::isfinite(desc.pivotX) || !std::isfinite(desc.pivotY))
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "pivot must be finite");
    }

    try
    {
        std::vector<std::byte> bytes(SpriteWire::PayloadBytes, std::byte{0});
        writeU16(bytes, 0U, SpriteWire::SchemaVersion);
        writeU16(bytes, 2U, 0U);
        writeF32(bytes, 4U, desc.u0);
        writeF32(bytes, 8U, desc.v0);
        writeF32(bytes, 12U, desc.u1);
        writeF32(bytes, 16U, desc.v1);
        writeF32(bytes, 20U, desc.pivotX);
        writeF32(bytes, 24U, desc.pivotY);
        writeF32(bytes, 28U, desc.pixelsPerUnit);
        // bytes 32..39 reserved zero
        return bytes;
    } catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory, "sprite payload allocation failed");
    }
}

Core::Result<SpritePayloadView> parseSpritePayload(std::span<const std::byte> payload)
{
    if (payload.size() != SpriteWire::PayloadBytes)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "sprite payload size mismatch");
    }
    SpritePayloadView view{};
    view.schemaVersion = readU16(payload, 0U);
    view.flags = readU16(payload, 2U);
    if (view.schemaVersion != SpriteWire::SchemaVersion)
    {
        return Core::failure(AssetFormatErrorCode::UnsupportedSchema, "unsupported sprite payload schema");
    }
    if (view.flags != 0U)
    {
        return Core::failure(AssetFormatErrorCode::UnsupportedValue, "unknown sprite flags");
    }
    view.u0 = readF32(payload, 4U);
    view.v0 = readF32(payload, 8U);
    view.u1 = readF32(payload, 12U);
    view.v1 = readF32(payload, 16U);
    view.pivotX = readF32(payload, 20U);
    view.pivotY = readF32(payload, 24U);
    view.pixelsPerUnit = readF32(payload, 28U);
    for (usize index = 32; index < SpriteWire::PayloadBytes; ++index)
    {
        if (readU8(payload, index) != 0U)
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout, "sprite reserved bytes must be zero");
        }
    }
    if (!isFiniteUv(view.u0) || !isFiniteUv(view.v0) || !isFiniteUv(view.u1) || !isFiniteUv(view.v1) ||
        !(view.u0 < view.u1) || !(view.v0 < view.v1))
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "sprite UV invalid");
    }
    if (!std::isfinite(view.pivotX) || !std::isfinite(view.pivotY))
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "sprite pivot invalid");
    }
    if (!(view.pixelsPerUnit > 0.0f) || !std::isfinite(view.pixelsPerUnit))
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "sprite pixelsPerUnit invalid");
    }
    return view;
}

Core::Result<std::vector<std::byte>> writeCookedSpriteAsset(Core::AssetId spriteId, const SpritePayloadDesc& desc,
                                                            TargetPlatform platform)
{
    if (!spriteId || !desc.textureId)
    {
        return Core::failure(AssetFormatErrorCode::InvalidIdentity, "sprite requires sprite id and texture id");
    }
    auto payload = writeSpritePayloadBytes(desc);
    if (!payload)
    {
        return Core::failure(std::move(payload.error()));
    }
    const std::array deps{CookedAssetWriteDependency{
        .assetId = desc.textureId,
        .expectedKind = AssetKind::Texture2D,
        .flags = DependencyFlags::Required,
    }};
    return writeCookedAssetBytes(CookedAssetWriteDesc{
        .assetKind = AssetKind::Sprite,
        .assetTypeVersion = SpriteWire::SchemaVersion,
        .targetPlatform = platform,
        .assetId = spriteId,
        .dependencies = deps,
        .payload = *payload,
        .payloadAlignment = 16,
        .computeContentHash = true,
    });
}

} // namespace Tina::AssetFormat
