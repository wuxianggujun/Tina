#include <tina/asset_format/Fx2DPayload.hpp>

#include <tina/asset_format/AssetFormatErrors.hpp>

#include <array>
#include <cmath>
#include <cstring>
#include <new>
#include <utility>

namespace Tina::AssetFormat {
namespace {

using Core::u8;
using Core::u16;
using Core::u32;
using Core::u64;
using Core::usize;

void putU16(std::vector<std::byte>& bytes, usize offset, u16 value) noexcept
{
    bytes[offset] = std::byte(value & 0xFFU);
    bytes[offset + 1U] = std::byte((value >> 8U) & 0xFFU);
}

void putU32(std::vector<std::byte>& bytes, usize offset, u32 value) noexcept
{
    for (usize index = 0; index < 4U; ++index) {
        bytes[offset + index] = std::byte((value >> (index * 8U)) & 0xFFU);
    }
}

void putU64(std::vector<std::byte>& bytes, usize offset, u64 value) noexcept
{
    for (usize index = 0; index < 8U; ++index) {
        bytes[offset + index] = std::byte((value >> (index * 8U)) & 0xFFU);
    }
}

void putF32(std::vector<std::byte>& bytes, usize offset, float value) noexcept
{
    u32 bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    putU32(bytes, offset, bits);
}

[[nodiscard]] u16 getU16(std::span<const std::byte> bytes, usize offset) noexcept
{
    return static_cast<u16>(std::to_integer<u8>(bytes[offset])) |
           static_cast<u16>(std::to_integer<u8>(bytes[offset + 1U]) << 8U);
}

[[nodiscard]] u32 getU32(std::span<const std::byte> bytes, usize offset) noexcept
{
    u32 value = 0;
    for (usize index = 0; index < 4U; ++index) {
        value |= static_cast<u32>(std::to_integer<u8>(bytes[offset + index])) <<
                 (index * 8U);
    }
    return value;
}

[[nodiscard]] u64 getU64(std::span<const std::byte> bytes, usize offset) noexcept
{
    u64 value = 0;
    for (usize index = 0; index < 8U; ++index) {
        value |= static_cast<u64>(std::to_integer<u8>(bytes[offset + index])) <<
                 (index * 8U);
    }
    return value;
}

[[nodiscard]] float getF32(std::span<const std::byte> bytes, usize offset) noexcept
{
    const u32 bits = getU32(bytes, offset);
    float value = 0.0F;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

[[nodiscard]] Core::Status validate(const Fx2DPayloadDesc& desc) noexcept
{
    const auto& particle = desc.particle;
    const auto& trail = desc.trail;
    if (!desc.spriteAssetId || particle.capacity == 0U ||
        particle.capacity > Fx2DWire::MaxParticleCapacity ||
        particle.count > particle.capacity || particle.count > Fx2DWire::MaxBurstCount ||
        particle.firstStableParticleKey == 0U || particle.spriteDependencyIndex != 0U ||
        trail.spriteDependencyIndex != 0U || trail.segmentCapacity == 0U ||
        trail.segmentCapacity > Fx2DWire::MaxTrailCapacity ||
        trail.stableEntityKeyBase == 0U) {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "Fx2D identity or capacity is invalid");
    }

    const std::array finiteValues{
        particle.originX,
        particle.originY,
        particle.positionOffsetMinX,
        particle.positionOffsetMinY,
        particle.positionOffsetMaxX,
        particle.positionOffsetMaxY,
        particle.velocityMinX,
        particle.velocityMinY,
        particle.velocityMaxX,
        particle.velocityMaxY,
        particle.lifetimeMinSeconds,
        particle.lifetimeMaxSeconds,
        particle.startWidthMeters,
        particle.startHeightMeters,
        particle.endWidthMeters,
        particle.endHeightMeters,
        particle.rotationRadians,
        trail.segmentLifetimeSeconds,
        trail.startWidthMeters,
        trail.endWidthMeters,
        trail.u0,
        trail.v0,
        trail.u1,
        trail.v1,
    };
    for (const float value : finiteValues) {
        if (!std::isfinite(value)) {
            return Core::failure(AssetFormatErrorCode::UnsupportedValue,
                                 "Fx2D values must be finite");
        }
    }
    if (!(particle.lifetimeMinSeconds > 0.0F) ||
        particle.lifetimeMaxSeconds < particle.lifetimeMinSeconds ||
        !(particle.startWidthMeters > 0.0F) || !(particle.startHeightMeters > 0.0F) ||
        !(particle.endWidthMeters > 0.0F) || !(particle.endHeightMeters > 0.0F) ||
        particle.positionOffsetMinX > particle.positionOffsetMaxX ||
        particle.positionOffsetMinY > particle.positionOffsetMaxY ||
        particle.velocityMinX > particle.velocityMaxX ||
        particle.velocityMinY > particle.velocityMaxY ||
        !(trail.segmentLifetimeSeconds > 0.0F) || !(trail.startWidthMeters > 0.0F) ||
        !(trail.endWidthMeters > 0.0F) || trail.u0 < 0.0F || trail.v0 < 0.0F ||
        trail.u1 > 1.0F || trail.v1 > 1.0F || trail.u0 >= trail.u1 ||
        trail.v0 >= trail.v1) {
        return Core::failure(AssetFormatErrorCode::UnsupportedValue,
                             "Fx2D range or size is invalid");
    }
    return Core::success();
}

} // namespace

Core::Status validateFx2DPayloadDesc(const Fx2DPayloadDesc& desc) noexcept
{
    return validate(desc);
}

Core::Result<std::vector<std::byte>> writeFx2DPayloadBytes(const Fx2DPayloadDesc& desc)
{
    if (auto status = validate(desc); !status) {
        return Core::failure(std::move(status.error()));
    }
    try {
        std::vector<std::byte> bytes(Fx2DWire::PayloadBytes, std::byte{0});
        usize offset = 0U;
        std::memcpy(bytes.data(), desc.spriteAssetId.bytes().data(), 16U);
        offset += 16U;
        putU32(bytes, offset, desc.particle.capacity);
        offset += 4U;
        putU32(bytes, offset, desc.particle.count);
        offset += 4U;
        putU64(bytes, offset, desc.particle.randomSeed);
        offset += 8U;
        putU32(bytes, offset, desc.particle.spriteDependencyIndex);
        offset += 4U;
        const std::array particleFloats{
            desc.particle.originX,
            desc.particle.originY,
            desc.particle.positionOffsetMinX,
            desc.particle.positionOffsetMinY,
            desc.particle.positionOffsetMaxX,
            desc.particle.positionOffsetMaxY,
            desc.particle.velocityMinX,
            desc.particle.velocityMinY,
            desc.particle.velocityMaxX,
            desc.particle.velocityMaxY,
            desc.particle.lifetimeMinSeconds,
            desc.particle.lifetimeMaxSeconds,
            desc.particle.startWidthMeters,
            desc.particle.startHeightMeters,
            desc.particle.endWidthMeters,
            desc.particle.endHeightMeters,
            desc.particle.rotationRadians,
        };
        for (const float value : particleFloats) {
            putF32(bytes, offset, value);
            offset += 4U;
        }
        putU32(bytes, offset, desc.particle.startColorRgba);
        offset += 4U;
        putU32(bytes, offset, desc.particle.endColorRgba);
        offset += 4U;
        putU64(bytes, offset, desc.particle.firstStableParticleKey);
        offset += 8U;
        putU16(bytes, offset, static_cast<u16>(desc.particle.sortingLayer));
        offset += 4U; // reserved u16 remains zero
        putU32(bytes, offset, static_cast<u32>(desc.particle.orderInLayer));
        offset += 4U;

        putU32(bytes, offset, desc.trail.segmentCapacity);
        offset += 4U;
        putF32(bytes, offset, desc.trail.segmentLifetimeSeconds);
        offset += 4U;
        putF32(bytes, offset, desc.trail.startWidthMeters);
        offset += 4U;
        putF32(bytes, offset, desc.trail.endWidthMeters);
        offset += 4U;
        putU64(bytes, offset, desc.trail.stableEntityKeyBase);
        offset += 8U;
        putU32(bytes, offset, desc.trail.spriteDependencyIndex);
        offset += 4U;
        const std::array trailFloats{
            desc.trail.u0,
            desc.trail.v0,
            desc.trail.u1,
            desc.trail.v1,
        };
        for (const float value : trailFloats) {
            putF32(bytes, offset, value);
            offset += 4U;
        }
        putU32(bytes, offset, desc.trail.colorRgba);
        offset += 4U;
        putU16(bytes, offset, static_cast<u16>(desc.trail.sortingLayer));
        offset += 4U; // reserved u16 remains zero
        putU32(bytes, offset, static_cast<u32>(desc.trail.orderInLayer));
        return bytes;
    } catch (const std::bad_alloc&) {
        return Core::failure(Core::CoreErrorCode::OutOfMemory,
                             "Fx2D payload allocation failed");
    }
}

Core::Result<Fx2DPayloadDesc> parseFx2DPayloadBytes(std::span<const std::byte> bytes)
{
    if (bytes.size() != Fx2DWire::PayloadBytes) {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "Fx2D payload byte count is invalid");
    }
    if (getU16(bytes, 122U) != 0U || getU16(bytes, 178U) != 0U) {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "Fx2D reserved fields must be zero");
    }

    Core::AssetId::Bytes spriteIdBytes{};
    std::memcpy(spriteIdBytes.data(), bytes.data(), spriteIdBytes.size());
    const auto spriteAssetId = Core::AssetId::fromBytes(spriteIdBytes);
    if (!spriteAssetId) {
        return Core::failure(AssetFormatErrorCode::InvalidIdentity,
                             "Fx2D sprite AssetId is zero");
    }

    Fx2DPayloadDesc desc{};
    desc.spriteAssetId = *spriteAssetId;
    usize offset = 16U;
    desc.particle.capacity = getU32(bytes, offset);
    offset += 4U;
    desc.particle.count = getU32(bytes, offset);
    offset += 4U;
    desc.particle.randomSeed = getU64(bytes, offset);
    offset += 8U;
    desc.particle.spriteDependencyIndex = getU32(bytes, offset);
    offset += 4U;
    const std::array particleFloats{
        &desc.particle.originX,
        &desc.particle.originY,
        &desc.particle.positionOffsetMinX,
        &desc.particle.positionOffsetMinY,
        &desc.particle.positionOffsetMaxX,
        &desc.particle.positionOffsetMaxY,
        &desc.particle.velocityMinX,
        &desc.particle.velocityMinY,
        &desc.particle.velocityMaxX,
        &desc.particle.velocityMaxY,
        &desc.particle.lifetimeMinSeconds,
        &desc.particle.lifetimeMaxSeconds,
        &desc.particle.startWidthMeters,
        &desc.particle.startHeightMeters,
        &desc.particle.endWidthMeters,
        &desc.particle.endHeightMeters,
        &desc.particle.rotationRadians,
    };
    for (float* value : particleFloats) {
        *value = getF32(bytes, offset);
        offset += 4U;
    }
    desc.particle.startColorRgba = getU32(bytes, offset);
    offset += 4U;
    desc.particle.endColorRgba = getU32(bytes, offset);
    offset += 4U;
    desc.particle.firstStableParticleKey = getU64(bytes, offset);
    offset += 8U;
    desc.particle.sortingLayer = static_cast<Core::i16>(getU16(bytes, offset));
    offset += 4U;
    desc.particle.orderInLayer = static_cast<Core::i32>(getU32(bytes, offset));
    offset += 4U;

    desc.trail.segmentCapacity = getU32(bytes, offset);
    offset += 4U;
    desc.trail.segmentLifetimeSeconds = getF32(bytes, offset);
    offset += 4U;
    desc.trail.startWidthMeters = getF32(bytes, offset);
    offset += 4U;
    desc.trail.endWidthMeters = getF32(bytes, offset);
    offset += 4U;
    desc.trail.stableEntityKeyBase = getU64(bytes, offset);
    offset += 8U;
    desc.trail.spriteDependencyIndex = getU32(bytes, offset);
    offset += 4U;
    const std::array trailFloats{
        &desc.trail.u0,
        &desc.trail.v0,
        &desc.trail.u1,
        &desc.trail.v1,
    };
    for (float* value : trailFloats) {
        *value = getF32(bytes, offset);
        offset += 4U;
    }
    desc.trail.colorRgba = getU32(bytes, offset);
    offset += 4U;
    desc.trail.sortingLayer = static_cast<Core::i16>(getU16(bytes, offset));
    offset += 4U;
    desc.trail.orderInLayer = static_cast<Core::i32>(getU32(bytes, offset));
    if (auto status = validate(desc); !status) {
        return Core::failure(std::move(status.error()));
    }
    return desc;
}

Core::Result<std::vector<std::byte>> writeCookedFx2DAsset(
    Core::AssetId assetId, const Fx2DPayloadDesc& desc, TargetPlatform platform)
{
    auto payload = writeFx2DPayloadBytes(desc);
    if (!payload) {
        return Core::failure(std::move(payload.error()));
    }
    const std::array dependencies{
        CookedAssetWriteDependency{
            .assetId = desc.spriteAssetId,
            .expectedKind = AssetKind::Sprite,
            .flags = DependencyFlags::Required,
        },
    };
    return writeCookedAssetBytes({
        .assetKind = AssetKind::Fx2D,
        .assetTypeVersion = Fx2DWire::SchemaVersion,
        .targetPlatform = platform,
        .assetId = assetId,
        .dependencies = dependencies,
        .payload = *payload,
        .payloadAlignment = 16,
        .computeContentHash = true,
    });
}

} // namespace Tina::AssetFormat
