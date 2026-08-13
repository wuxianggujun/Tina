#include <tina/asset_format/AssetFormatErrors.hpp>
#include <tina/asset_format/Fx2DPayload.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

namespace Tina::AssetFormat {
namespace {

[[nodiscard]] Core::AssetId assetId(Core::u8 seed)
{
    Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(seed);
    bytes[15] = static_cast<std::byte>(seed ^ 0x5AU);
    return *Core::AssetId::fromBytes(bytes);
}

[[nodiscard]] Fx2DPayloadDesc validFx()
{
    Fx2DPayloadDesc desc{};
    desc.spriteAssetId = assetId(2U);
    desc.particle.capacity = 12;
    desc.particle.count = 10;
    desc.particle.randomSeed = 1414090305U;
    desc.particle.firstStableParticleKey = 0x100000000ULL;
    desc.particle.originX = 4.0F;
    desc.particle.originY = 2.0F;
    desc.particle.positionOffsetMinX = -0.5F;
    desc.particle.positionOffsetMaxX = 0.5F;
    desc.particle.velocityMinY = -0.1F;
    desc.particle.velocityMaxY = 0.2F;
    desc.particle.lifetimeMinSeconds = 2.0F;
    desc.particle.lifetimeMaxSeconds = 3.0F;
    desc.particle.startWidthMeters = 0.2F;
    desc.particle.startHeightMeters = 0.3F;
    desc.particle.endWidthMeters = 0.1F;
    desc.particle.endHeightMeters = 0.15F;
    desc.particle.startColorRgba = 0xE6FFFFFFU;
    desc.particle.endColorRgba = 0x7900FFFFU;
    desc.particle.sortingLayer = 2;
    desc.particle.orderInLayer = 11;
    desc.trail.segmentCapacity = 8;
    desc.trail.segmentLifetimeSeconds = 10.0F;
    desc.trail.startWidthMeters = 0.18F;
    desc.trail.endWidthMeters = 0.04F;
    desc.trail.stableEntityKeyBase = 0x200000000ULL;
    desc.trail.colorRgba = 0xD2B0FFFFU;
    desc.trail.sortingLayer = 1;
    desc.trail.orderInLayer = 8;
    return desc;
}

TEST(Fx2DPayloadTests, RoundTripsFixedPayloadAndRequiredSpriteDependency)
{
    const Fx2DPayloadDesc desc = validFx();
    auto payload = writeFx2DPayloadBytes(desc);
    ASSERT_TRUE(payload) << payload.error().message;
    ASSERT_EQ(payload->size(), Fx2DWire::PayloadBytes);
    auto parsed = parseFx2DPayloadBytes(*payload);
    ASSERT_TRUE(parsed) << parsed.error().message;
    EXPECT_EQ(parsed->spriteAssetId, desc.spriteAssetId);
    EXPECT_EQ(parsed->particle.capacity, 12U);
    EXPECT_EQ(parsed->particle.count, 10U);
    EXPECT_EQ(parsed->particle.firstStableParticleKey, 0x100000000ULL);
    EXPECT_FLOAT_EQ(parsed->particle.lifetimeMaxSeconds, 3.0F);
    EXPECT_EQ(parsed->trail.segmentCapacity, 8U);
    EXPECT_EQ(parsed->trail.stableEntityKeyBase, 0x200000000ULL);

    const Core::AssetId fxId = assetId(9U);
    auto cooked = writeCookedFx2DAsset(fxId, desc, TargetPlatform::WindowsX64);
    ASSERT_TRUE(cooked) << cooked.error().message;
    auto file = parseCookedAssetView(*cooked);
    ASSERT_TRUE(file) << file.error().message;
    EXPECT_EQ(file->header().assetKind, AssetKind::Fx2D);
    EXPECT_EQ(file->header().assetId, fxId);
    ASSERT_EQ(file->header().dependencyCount, 1U);
    const auto dependency = file->dependency(0U);
    ASSERT_TRUE(dependency);
    EXPECT_EQ(dependency->assetId, desc.spriteAssetId);
    EXPECT_EQ(dependency->expectedKind, AssetKind::Sprite);
    EXPECT_EQ(dependency->flags, DependencyFlags::Required);
}

TEST(Fx2DPayloadTests, RejectsInvalidRangesAndReservedWireFields)
{
    auto invalid = validFx();
    invalid.particle.count = invalid.particle.capacity + 1U;
    EXPECT_FALSE(writeFx2DPayloadBytes(invalid));
    invalid = validFx();
    invalid.particle.velocityMinX = 1.0F;
    invalid.particle.velocityMaxX = -1.0F;
    EXPECT_FALSE(writeFx2DPayloadBytes(invalid));
    invalid = validFx();
    invalid.trail.u1 = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(writeFx2DPayloadBytes(invalid));

    auto payload = writeFx2DPayloadBytes(validFx());
    ASSERT_TRUE(payload);
    (*payload)[122] = std::byte{1};
    auto reserved = parseFx2DPayloadBytes(*payload);
    ASSERT_FALSE(reserved);
    EXPECT_EQ(reserved.error().code, AssetFormatErrorCode::InvalidLayout);
    payload->pop_back();
    EXPECT_FALSE(parseFx2DPayloadBytes(*payload));
}

} // namespace
} // namespace Tina::AssetFormat
