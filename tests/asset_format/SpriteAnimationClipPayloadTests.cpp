#include <tina/asset_format/AssetFormat.hpp>
#include <tina/asset_format/AssetFormatErrors.hpp>
#include <tina/asset_format/SpriteAnimationClipPayload.hpp>
#include <tina/core/id/AssetId.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstring>
#include <limits>
#include <vector>

namespace Tina::AssetFormat {
namespace {

[[nodiscard]] Core::AssetId::Bytes idBytes(Core::u8 seed)
{
    Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(seed);
    bytes[15] = static_cast<std::byte>(seed ^ 0x5AU);
    return bytes;
}

void putU32(std::vector<std::byte>& bytes, Core::usize offset, Core::u32 value)
{
    for (Core::usize index = 0; index < sizeof(Core::u32); ++index)
    {
        bytes.at(offset + index) = static_cast<std::byte>((value >> (index * 8U)) & 0xFFU);
    }
}

void expectParseError(const std::vector<std::byte>& payload, Core::ErrorCode expectedCode)
{
    auto parsed = parseSpriteAnimationClipPayload(payload);
    ASSERT_FALSE(parsed.has_value());
    EXPECT_EQ(parsed.error().code, expectedCode);
}

TEST(SpriteAnimationClipPayloadTests, RoundTripsFrameOrderThroughSortedSpriteDependencies)
{
    const auto spriteA = *Core::AssetId::fromBytes(idBytes(1U));
    const auto spriteB = *Core::AssetId::fromBytes(idBytes(2U));
    const auto clipId = *Core::AssetId::fromBytes(idBytes(9U));
    const std::array frames{
        SpriteAnimationFrameDesc{.spriteId = spriteB, .durationSeconds = 0.125F},
        SpriteAnimationFrameDesc{.spriteId = spriteA, .durationSeconds = 0.25F},
        SpriteAnimationFrameDesc{.spriteId = spriteB, .durationSeconds = 0.5F},
    };
    const SpriteAnimationClipPayloadDesc desc{
        .playbackMode = SpriteAnimationPlaybackMode::PingPong,
        .frames = frames,
    };

    auto dependencies = makeSpriteAnimationClipDependencies(desc);
    ASSERT_TRUE(dependencies.has_value()) << dependencies.error().message;
    ASSERT_EQ(dependencies->size(), 2U);
    EXPECT_EQ((*dependencies)[0].assetId, spriteA);
    EXPECT_EQ((*dependencies)[1].assetId, spriteB);
    EXPECT_EQ((*dependencies)[0].expectedKind, AssetKind::Sprite);

    auto payload = writeSpriteAnimationClipPayloadBytes(desc);
    ASSERT_TRUE(payload.has_value()) << payload.error().message;
    // 0.125F is IEEE-754 0x3e000000. Cooked payloads are always little-endian.
    EXPECT_EQ(std::to_integer<unsigned char>((*payload)[20U]), 0x00U);
    EXPECT_EQ(std::to_integer<unsigned char>((*payload)[21U]), 0x00U);
    EXPECT_EQ(std::to_integer<unsigned char>((*payload)[22U]), 0x00U);
    EXPECT_EQ(std::to_integer<unsigned char>((*payload)[23U]), 0x3EU);
    auto view = parseSpriteAnimationClipPayload(*payload);
    ASSERT_TRUE(view.has_value()) << view.error().message;
    EXPECT_EQ(view->schemaVersion, SpriteAnimationClipWire::SchemaVersion);
    EXPECT_EQ(view->playbackMode, SpriteAnimationPlaybackMode::PingPong);
    EXPECT_EQ(view->frameCount, 3U);
    EXPECT_EQ(view->spriteDependencyCount, 2U);
    ASSERT_TRUE(view->frame(0U).has_value());
    ASSERT_TRUE(view->frame(1U).has_value());
    ASSERT_TRUE(view->frame(2U).has_value());
    EXPECT_EQ(view->frame(0U)->spriteDependencyIndex, 1U);
    EXPECT_EQ(view->frame(1U)->spriteDependencyIndex, 0U);
    EXPECT_EQ(view->frame(2U)->spriteDependencyIndex, 1U);
    EXPECT_FLOAT_EQ(view->frame(0U)->durationSeconds, 0.125F);
    EXPECT_FALSE(view->frame(3U).has_value());

    auto cooked = writeCookedSpriteAnimationClipAsset(clipId, desc);
    ASSERT_TRUE(cooked.has_value()) << cooked.error().message;
    auto asset = parseCookedAssetView(*cooked);
    ASSERT_TRUE(asset.has_value()) << asset.error().message;
    EXPECT_EQ(asset->header().assetKind, AssetKind::SpriteAnimationClip);
    EXPECT_EQ(asset->header().assetTypeVersion, SpriteAnimationClipWire::SchemaVersion);
    ASSERT_EQ(asset->header().dependencyCount, 2U);
    ASSERT_TRUE(asset->dependency(0U).has_value());
    ASSERT_TRUE(asset->dependency(1U).has_value());
    EXPECT_EQ(asset->dependency(0U)->assetId, spriteA);
    EXPECT_EQ(asset->dependency(1U)->assetId, spriteB);
    EXPECT_EQ(asset->dependency(1U)->expectedKind, AssetKind::Sprite);
    EXPECT_TRUE(verifyCookedAssetContentHash(*asset).has_value());
}

TEST(SpriteAnimationClipPayloadTests, SupportsEveryPlaybackMode)
{
    const auto spriteId = *Core::AssetId::fromBytes(idBytes(1U));
    const std::array frames{
        SpriteAnimationFrameDesc{.spriteId = spriteId, .durationSeconds = 0.1F},
    };
    constexpr std::array modes{
        SpriteAnimationPlaybackMode::Once,
        SpriteAnimationPlaybackMode::Loop,
        SpriteAnimationPlaybackMode::PingPong,
    };
    for (const auto mode : modes)
    {
        auto payload = writeSpriteAnimationClipPayloadBytes(
            SpriteAnimationClipPayloadDesc{.playbackMode = mode, .frames = frames});
        ASSERT_TRUE(payload.has_value());
        auto view = parseSpriteAnimationClipPayload(*payload);
        ASSERT_TRUE(view.has_value());
        EXPECT_EQ(view->playbackMode, mode);
    }
}

TEST(SpriteAnimationClipPayloadTests, RejectsInvalidDescriptions)
{
    EXPECT_FALSE(writeSpriteAnimationClipPayloadBytes({}).has_value());

    const auto spriteId = *Core::AssetId::fromBytes(idBytes(1U));
    std::array frames{
        SpriteAnimationFrameDesc{.spriteId = spriteId, .durationSeconds = 0.1F},
    };
    auto desc = SpriteAnimationClipPayloadDesc{.frames = frames};

    desc.playbackMode = static_cast<SpriteAnimationPlaybackMode>(99U);
    EXPECT_FALSE(writeSpriteAnimationClipPayloadBytes(desc).has_value());
    desc.playbackMode = SpriteAnimationPlaybackMode::Loop;

    frames[0].spriteId = {};
    EXPECT_FALSE(writeSpriteAnimationClipPayloadBytes(desc).has_value());
    frames[0].spriteId = spriteId;

    frames[0].durationSeconds = 0.0F;
    EXPECT_FALSE(writeSpriteAnimationClipPayloadBytes(desc).has_value());
    frames[0].durationSeconds = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(writeSpriteAnimationClipPayloadBytes(desc).has_value());
    frames[0].durationSeconds = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(writeSpriteAnimationClipPayloadBytes(desc).has_value());
}

TEST(SpriteAnimationClipPayloadTests, RejectsCorruptHeaderFrameAndDuration)
{
    const auto spriteId = *Core::AssetId::fromBytes(idBytes(1U));
    const std::array frames{
        SpriteAnimationFrameDesc{.spriteId = spriteId, .durationSeconds = 0.1F},
    };
    auto payload = writeSpriteAnimationClipPayloadBytes(
        SpriteAnimationClipPayloadDesc{.frames = frames});
    ASSERT_TRUE(payload.has_value());

    auto corrupted = *payload;
    corrupted[2] = std::byte{99};
    expectParseError(corrupted, AssetFormatErrorCode::UnsupportedValue);

    corrupted = *payload;
    corrupted[3] = std::byte{1};
    expectParseError(corrupted, AssetFormatErrorCode::UnsupportedValue);

    corrupted = *payload;
    putU32(corrupted, 16U, 1U);
    expectParseError(corrupted, AssetFormatErrorCode::InvalidDependency);

    corrupted = *payload;
    const float zeroDuration = 0.0F;
    std::memcpy(corrupted.data() + 20U, &zeroDuration, sizeof(float));
    expectParseError(corrupted, AssetFormatErrorCode::InvalidLayout);

    corrupted = *payload;
    corrupted.pop_back();
    expectParseError(corrupted, AssetFormatErrorCode::InvalidLayout);
}

} // namespace
} // namespace Tina::AssetFormat
