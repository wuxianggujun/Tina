#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/AssetTypedViews.hpp>
#include <tina/asset/CookedAssetFile.hpp>
#include <tina/asset_format/AssetFormat.hpp>
#include <tina/asset_format/SpriteAnimationClipPayload.hpp>
#include <tina/core/id/AssetId.hpp>

#include <gtest/gtest.h>

#include <array>
#include <memory_resource>
#include <vector>

namespace Tina::Asset {
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

TEST(SpriteAnimationClipTypedViewTests, ResolvesValidatedSpriteDependencyIndices)
{
    std::pmr::unsynchronized_pool_resource memory;
    const auto spriteA = *Core::AssetId::fromBytes(idBytes(1U));
    const auto spriteB = *Core::AssetId::fromBytes(idBytes(2U));
    const auto clipId = *Core::AssetId::fromBytes(idBytes(9U));
    const std::array frames{
        AssetFormat::SpriteAnimationFrameDesc{.spriteId = spriteB, .durationSeconds = 0.08F},
        AssetFormat::SpriteAnimationFrameDesc{.spriteId = spriteA, .durationSeconds = 0.12F},
    };
    auto cooked = AssetFormat::writeCookedSpriteAnimationClipAsset(
        clipId, AssetFormat::SpriteAnimationClipPayloadDesc{
                    .playbackMode = AssetFormat::SpriteAnimationPlaybackMode::Loop,
                    .frames = frames,
                });
    ASSERT_TRUE(cooked.has_value()) << cooked.error().message;
    auto file = makeCookedAssetFileFromBytes(
        std::pmr::vector<std::byte>(cooked->begin(), cooked->end(), &memory),
        CookedAssetFileLoadConfig{.memoryResource = &memory});
    ASSERT_TRUE(file.has_value()) << file.error().message;

    auto clip = parseSpriteAnimationClipFromCooked(*file);
    ASSERT_TRUE(clip.has_value()) << clip.error().message;
    ASSERT_TRUE(clip->frame(0U).has_value());
    const auto dependency = file->dependency(clip->frame(0U)->spriteDependencyIndex);
    ASSERT_TRUE(dependency.has_value());
    EXPECT_EQ(dependency->assetId, spriteB);
    EXPECT_EQ(dependency->expectedKind, AssetFormat::AssetKind::Sprite);
}

TEST(SpriteAnimationClipTypedViewTests, RejectsNonSpriteDependency)
{
    std::pmr::unsynchronized_pool_resource memory;
    const auto spriteId = *Core::AssetId::fromBytes(idBytes(1U));
    const auto clipId = *Core::AssetId::fromBytes(idBytes(9U));
    const std::array frames{
        AssetFormat::SpriteAnimationFrameDesc{.spriteId = spriteId, .durationSeconds = 0.1F},
    };
    auto payload = AssetFormat::writeSpriteAnimationClipPayloadBytes(
        AssetFormat::SpriteAnimationClipPayloadDesc{.frames = frames});
    ASSERT_TRUE(payload.has_value());
    const std::array dependencies{
        AssetFormat::CookedAssetWriteDependency{
            .assetId = spriteId,
            .expectedKind = AssetFormat::AssetKind::Texture2D,
            .flags = AssetFormat::DependencyFlags::Required,
        },
    };
    auto cooked = AssetFormat::writeCookedAssetBytes(AssetFormat::CookedAssetWriteDesc{
        .assetKind = AssetFormat::AssetKind::SpriteAnimationClip,
        .assetTypeVersion = AssetFormat::SpriteAnimationClipWire::SchemaVersion,
        .assetId = clipId,
        .dependencies = dependencies,
        .payload = *payload,
    });
    ASSERT_TRUE(cooked.has_value()) << cooked.error().message;
    auto file = makeCookedAssetFileFromBytes(
        std::pmr::vector<std::byte>(cooked->begin(), cooked->end(), &memory),
        CookedAssetFileLoadConfig{.memoryResource = &memory});
    ASSERT_TRUE(file.has_value()) << file.error().message;

    auto clip = parseSpriteAnimationClipFromCooked(*file);
    ASSERT_FALSE(clip.has_value());
    EXPECT_EQ(clip.error().code, AssetErrorCode::CatalogEntryMismatch);
}

TEST(SpriteAnimationClipTypedViewTests, RejectsUnusedCookedDependency)
{
    std::pmr::unsynchronized_pool_resource memory;
    const auto spriteA = *Core::AssetId::fromBytes(idBytes(1U));
    const auto spriteB = *Core::AssetId::fromBytes(idBytes(2U));
    const auto clipId = *Core::AssetId::fromBytes(idBytes(9U));
    const std::array frames{
        AssetFormat::SpriteAnimationFrameDesc{.spriteId = spriteA, .durationSeconds = 0.1F},
        AssetFormat::SpriteAnimationFrameDesc{.spriteId = spriteA, .durationSeconds = 0.2F},
    };
    auto payload = AssetFormat::writeSpriteAnimationClipPayloadBytes(
        AssetFormat::SpriteAnimationClipPayloadDesc{.frames = frames});
    ASSERT_TRUE(payload.has_value());
    // Keep both frame indices at zero but claim two dependencies in the payload.
    putU32(*payload, 8U, 2U);
    ASSERT_TRUE(AssetFormat::parseSpriteAnimationClipPayload(*payload).has_value());

    const std::array dependencies{
        AssetFormat::CookedAssetWriteDependency{
            .assetId = spriteA,
            .expectedKind = AssetFormat::AssetKind::Sprite,
            .flags = AssetFormat::DependencyFlags::Required,
        },
        AssetFormat::CookedAssetWriteDependency{
            .assetId = spriteB,
            .expectedKind = AssetFormat::AssetKind::Sprite,
            .flags = AssetFormat::DependencyFlags::Required,
        },
    };
    auto cooked = AssetFormat::writeCookedAssetBytes(AssetFormat::CookedAssetWriteDesc{
        .assetKind = AssetFormat::AssetKind::SpriteAnimationClip,
        .assetTypeVersion = AssetFormat::SpriteAnimationClipWire::SchemaVersion,
        .assetId = clipId,
        .dependencies = dependencies,
        .payload = *payload,
    });
    ASSERT_TRUE(cooked.has_value()) << cooked.error().message;
    auto file = makeCookedAssetFileFromBytes(
        std::pmr::vector<std::byte>(cooked->begin(), cooked->end(), &memory),
        CookedAssetFileLoadConfig{.memoryResource = &memory});
    ASSERT_TRUE(file.has_value()) << file.error().message;

    auto clip = parseSpriteAnimationClipFromCooked(*file);
    ASSERT_FALSE(clip.has_value());
    EXPECT_EQ(clip.error().code, AssetErrorCode::CatalogEntryMismatch);
}

} // namespace
} // namespace Tina::Asset
