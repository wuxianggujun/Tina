#include <tina/asset_format/Prefab2DPayload.hpp>
#include <tina/asset_format/World2DSnapshot.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <vector>

namespace Tina::AssetFormat {
namespace {

[[nodiscard]] Core::AssetId assetId(Core::u8 seed)
{
    Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(seed);
    bytes[15] = static_cast<std::byte>(seed ^ 0x3CU);
    return *Core::AssetId::fromBytes(bytes);
}

TEST(Prefab2DPayloadTests, RejectsNestedPrefabInstance)
{
    const std::array entities{
        World2DEntityDesc{
            .stableEntityId = 1,
            .nodeKind = World2DNodeKind::Node2D,
        },
        World2DEntityDesc{
            .stableEntityId = 2,
            .parentStableEntityId = 1,
            .nodeKind = World2DNodeKind::PrefabInstance2D,
            .resource = World2DResourceNodeDesc{.assetId = assetId(9)},
        },
    };
    const auto status = validatePrefab2DSnapshot(entities);
    ASSERT_FALSE(status);
}

TEST(Prefab2DPayloadTests, WritesAndParsesSingleRootSpriteSubtree)
{
    const std::array entities{
        World2DEntityDesc{
            .stableEntityId = 4,
            .nodeKind = World2DNodeKind::Sprite2D,
            .sprite = World2DSpriteDesc{.spriteId = assetId(1)},
        },
        World2DEntityDesc{
            .stableEntityId = 5,
            .parentStableEntityId = 4,
            .nodeKind = World2DNodeKind::Node2D,
        },
    };
    auto bytes = writePrefab2DPayloadBytes(entities);
    ASSERT_TRUE(bytes) << (bytes ? "" : bytes.error().message);
    std::vector<World2DEntityDesc> storage;
    auto parsed = parsePrefab2DPayload(*bytes, storage);
    ASSERT_TRUE(parsed) << (parsed ? "" : parsed.error().message);
    ASSERT_EQ(storage.size(), 2U);
    EXPECT_EQ(storage.front().nodeKind, World2DNodeKind::Sprite2D);
    EXPECT_EQ(storage.front().sprite->spriteId, assetId(1));
    auto dependencies = collectPrefab2DDependencies(storage);
    ASSERT_TRUE(dependencies) << (dependencies ? "" : dependencies.error().message);
    ASSERT_EQ(dependencies->size(), 1U);
    EXPECT_EQ(dependencies->front().assetId, assetId(1));
    EXPECT_EQ(dependencies->front().expectedKind, AssetKind::Sprite);
}

TEST(Prefab2DPayloadTests, RejectsGameplayBytes)
{
    const std::array entities{
        World2DEntityDesc{
            .stableEntityId = 1,
            .nodeKind = World2DNodeKind::Node2D,
        },
    };
    const std::array gameplay{std::byte{1}};
    auto bytes = writeWorld2DSnapshotBytes(World2DSnapshotDesc{
        .entities = entities,
        .gameplaySchema = 1,
        .gameplayVersion = 1,
        .gameplayBytes = gameplay,
    });
    ASSERT_TRUE(bytes);
    std::vector<World2DEntityDesc> storage;
    auto parsed = parsePrefab2DPayload(*bytes, storage);
    ASSERT_FALSE(parsed);
}

} // namespace
} // namespace Tina::AssetFormat
