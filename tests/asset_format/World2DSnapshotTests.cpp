#include <tina/asset_format/AssetFormatErrors.hpp>
#include <tina/asset_format/World2DSnapshot.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

namespace Tina::AssetFormat {
namespace {

[[nodiscard]] Core::AssetId assetId(Core::u8 seed)
{
    Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(seed);
    bytes[15] = static_cast<std::byte>(seed ^ 0xA5U);
    return *Core::AssetId::fromBytes(bytes);
}

[[nodiscard]] World2DEntityDesc fullyAuthoredEntity()
{
    return World2DEntityDesc{
        .stableEntityId = 10,
        .positionX = 1.25F,
        .positionY = -2.5F,
        .positionZ = 0.5F,
        .rotationZ = 0.5F,
        .rotationW = 0.8660254F,
        .scaleX = 2.0F,
        .scaleY = 2.0F,
        .sprite =
            World2DSpriteDesc{
                .spriteId = assetId(1),
                .normalTextureId = assetId(2),
                .overrides = World2DSpriteOverrideFlags::Size | World2DSpriteOverrideFlags::Pivot |
                             World2DSpriteOverrideFlags::UvRect,
                .sizeX = 3.0F,
                .sizeY = 4.0F,
                .pivotX = 0.25F,
                .pivotY = 0.75F,
                .uvU0 = 0.1F,
                .uvV0 = 0.2F,
                .uvU1 = 0.8F,
                .uvV1 = 0.9F,
                .colorRed = 12,
                .colorGreen = 34,
                .colorBlue = 56,
                .colorAlpha = 78,
                .sortingLayer = -3,
                .orderInLayer = 42,
                .flipX = true,
                .visible = false,
            },
        .camera =
            World2DCameraDesc{
                .projection = World2DCameraProjectionKind::PixelPerfect,
                .pixelSnap = World2DPixelSnapPolicy::CameraAndSprites,
                .viewportX = 0.1F,
                .viewportY = 0.2F,
                .viewportWidth = 0.7F,
                .viewportHeight = 0.6F,
                .referencePixelsPerMeter = 32.0F,
                .referenceHeightPixels = 360,
                .active = false,
            },
        .pointLight =
            World2DPointLightDesc{
                .colorRed = 0.25F,
                .colorGreen = 0.5F,
                .colorBlue = 0.75F,
                .intensity = 3.0F,
                .radiusMeters = 8.0F,
                .sourceRadiusMeters = 2.0F,
                .active = false,
            },
        .shadowOccluder =
            World2DShadowOccluderDesc{
                .localStartX = -2.0F,
                .localStartY = 1.0F,
                .localEndX = 3.0F,
                .localEndY = 4.0F,
                .active = false,
            },
    };
}

TEST(World2DSnapshotTests, RoundTripsAllComponentsAndGameplayDeterministically)
{
    const std::array entities{
        fullyAuthoredEntity(),
        World2DEntityDesc{
            .stableEntityId = 20,
            .parentStableEntityId = 10,
            .positionX = 5.0F,
        },
    };
    const std::array gameplay{
        std::byte{0x10},
        std::byte{0x20},
        std::byte{0x30},
        std::byte{0x40},
    };
    const World2DSnapshotDesc desc{
        .entities = entities,
        .gameplaySchema = 77,
        .gameplayVersion = 3,
        .gameplayBytes = gameplay,
    };

    auto first = writeWorld2DSnapshotBytes(desc);
    auto second = writeWorld2DSnapshotBytes(desc);
    ASSERT_TRUE(first) << (first ? "" : first.error().message);
    ASSERT_TRUE(second) << (second ? "" : second.error().message);
    EXPECT_EQ(*first, *second);

    std::vector<World2DEntityDesc> storage;
    auto parsed = parseWorld2DSnapshot(*first, storage);
    ASSERT_TRUE(parsed) << (parsed ? "" : parsed.error().message);
    EXPECT_EQ(parsed->schemaVersion, World2DSnapshotWire::SchemaVersion);
    ASSERT_EQ(storage.size(), entities.size());
    EXPECT_EQ(storage[0], entities[0]);
    EXPECT_EQ(storage[1], entities[1]);
    EXPECT_EQ(parsed->gameplaySchema, 77U);
    EXPECT_EQ(parsed->gameplayVersion, 3U);
    ASSERT_EQ(parsed->gameplayBytes.size(), gameplay.size());
    EXPECT_TRUE(std::equal(gameplay.begin(), gameplay.end(), parsed->gameplayBytes.begin()));

    auto rewritten = writeWorld2DSnapshotBytes(World2DSnapshotDesc{
        .entities = parsed->entities,
        .gameplaySchema = parsed->gameplaySchema,
        .gameplayVersion = parsed->gameplayVersion,
        .gameplayBytes = parsed->gameplayBytes,
    });
    ASSERT_TRUE(rewritten);
    EXPECT_EQ(*rewritten, *first);
}

TEST(World2DSnapshotTests, RejectsOldSchemaAndPreservesCallerStorage)
{
    const std::array entities{World2DEntityDesc{.stableEntityId = 1}};
    auto payload = writeWorld2DSnapshotBytes(World2DSnapshotDesc{.entities = entities});
    ASSERT_TRUE(payload);
    (*payload)[0] = std::byte{0};
    (*payload)[1] = std::byte{0};

    std::vector<World2DEntityDesc> storage{World2DEntityDesc{.stableEntityId = 99}};
    auto parsed = parseWorld2DSnapshot(*payload, storage);
    ASSERT_FALSE(parsed);
    EXPECT_EQ(parsed.error().code, AssetFormatErrorCode::UnsupportedSchema);
    ASSERT_EQ(storage.size(), 1U);
    EXPECT_EQ(storage[0].stableEntityId, 99U);
}

TEST(World2DSnapshotTests, RejectsDuplicateAndForwardParentStableIds)
{
    const std::array duplicate{
        World2DEntityDesc{.stableEntityId = 1},
        World2DEntityDesc{.stableEntityId = 1},
    };
    EXPECT_FALSE(writeWorld2DSnapshotBytes(World2DSnapshotDesc{.entities = duplicate}));

    const std::array forwardParent{
        World2DEntityDesc{.stableEntityId = 1, .parentStableEntityId = 2},
        World2DEntityDesc{.stableEntityId = 2},
    };
    EXPECT_FALSE(writeWorld2DSnapshotBytes(World2DSnapshotDesc{.entities = forwardParent}));
}

TEST(World2DSnapshotTests, RejectsNonCanonicalAbsentComponentAtomically)
{
    const std::array entities{World2DEntityDesc{.stableEntityId = 1}};
    auto payload = writeWorld2DSnapshotBytes(World2DSnapshotDesc{.entities = entities});
    ASSERT_TRUE(payload);
    constexpr Core::usize SpriteAssetIdOffset = World2DSnapshotWire::HeaderBytes + 56U;
    (*payload)[SpriteAssetIdOffset] = std::byte{1};

    std::vector<World2DEntityDesc> storage{World2DEntityDesc{.stableEntityId = 88}};
    auto parsed = parseWorld2DSnapshot(*payload, storage);
    ASSERT_FALSE(parsed);
    EXPECT_EQ(parsed.error().code, AssetFormatErrorCode::InvalidLayout);
    ASSERT_EQ(storage.size(), 1U);
    EXPECT_EQ(storage[0].stableEntityId, 88U);
}

TEST(World2DSnapshotTests, RequiresGameplayIdentityExactlyWhenBlobIsPresent)
{
    const std::array gameplay{std::byte{1}};
    EXPECT_FALSE(writeWorld2DSnapshotBytes(World2DSnapshotDesc{
        .gameplaySchema = 1,
        .gameplayVersion = 1,
    }));
    EXPECT_FALSE(writeWorld2DSnapshotBytes(World2DSnapshotDesc{
        .gameplayBytes = gameplay,
    }));
}

} // namespace
} // namespace Tina::AssetFormat
