#pragma once

#include <tina/asset_format/AssetFormat.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/id/AssetId.hpp>

#include <span>
#include <vector>

namespace Tina::AssetFormat {

namespace Fx2DWire {
inline constexpr Core::u16 SchemaVersion = 1;
inline constexpr Core::u32 PayloadBytes = 184;
inline constexpr Core::u32 MaxBurstCount = 4096;
inline constexpr Core::u32 MaxParticleCapacity = 65536;
inline constexpr Core::u32 MaxTrailCapacity = 65536;
}

struct Fx2DParticleDesc final {
    Core::u32 capacity = 1;
    Core::u32 count = 1;
    Core::u64 randomSeed = 0;
    Core::u64 firstStableParticleKey = 1;
    Core::u32 spriteDependencyIndex = 0;
    float originX = 0.0F;
    float originY = 0.0F;
    float positionOffsetMinX = 0.0F;
    float positionOffsetMinY = 0.0F;
    float positionOffsetMaxX = 0.0F;
    float positionOffsetMaxY = 0.0F;
    float velocityMinX = 0.0F;
    float velocityMinY = 0.0F;
    float velocityMaxX = 0.0F;
    float velocityMaxY = 0.0F;
    float lifetimeMinSeconds = 1.0F;
    float lifetimeMaxSeconds = 1.0F;
    float startWidthMeters = 1.0F;
    float startHeightMeters = 1.0F;
    float endWidthMeters = 1.0F;
    float endHeightMeters = 1.0F;
    Core::u32 startColorRgba = 0xFFFFFFFFU;
    Core::u32 endColorRgba = 0xFFFFFFFFU;
    float rotationRadians = 0.0F;
    Core::i16 sortingLayer = 0;
    Core::i32 orderInLayer = 0;
};

struct Fx2DTrailDesc final {
    Core::u32 segmentCapacity = 1;
    float segmentLifetimeSeconds = 1.0F;
    float startWidthMeters = 1.0F;
    float endWidthMeters = 1.0F;
    Core::u64 stableEntityKeyBase = 1;
    Core::u32 spriteDependencyIndex = 0;
    float u0 = 0.0F;
    float v0 = 0.0F;
    float u1 = 1.0F;
    float v1 = 1.0F;
    Core::u32 colorRgba = 0xFFFFFFFFU;
    Core::i16 sortingLayer = 0;
    Core::i32 orderInLayer = 0;
};

struct Fx2DPayloadDesc final {
    Core::AssetId spriteAssetId{};
    Fx2DParticleDesc particle{};
    Fx2DTrailDesc trail{};
};

[[nodiscard]] Core::Status validateFx2DPayloadDesc(const Fx2DPayloadDesc& desc) noexcept;
[[nodiscard]] Core::Result<std::vector<std::byte>> writeFx2DPayloadBytes(
    const Fx2DPayloadDesc& desc);
[[nodiscard]] Core::Result<Fx2DPayloadDesc> parseFx2DPayloadBytes(
    std::span<const std::byte> bytes);
[[nodiscard]] Core::Result<std::vector<std::byte>> writeCookedFx2DAsset(
    Core::AssetId assetId, const Fx2DPayloadDesc& desc, TargetPlatform platform);

} // namespace Tina::AssetFormat
