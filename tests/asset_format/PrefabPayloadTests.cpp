#include <tina/asset_format/AssetFormat.hpp>
#include <tina/asset_format/PrefabPayload.hpp>
#include <tina/core/id/AssetId.hpp>

#include <gtest/gtest.h>

#include <array>
#include <vector>

namespace Tina::AssetFormat {
namespace {

[[nodiscard]] Core::AssetId::Bytes idBytes(Core::u8 seed)
{
    Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(seed);
    bytes[15] = static_cast<std::byte>(seed ^ 0xE4U);
    return bytes;
}

TEST(PrefabPayloadTests, RoundTripsSingleRootMeshNode)
{
    const auto meshId = *Core::AssetId::fromBytes(idBytes(0x12));
    const auto materialId = *Core::AssetId::fromBytes(idBytes(0x11));
    const std::array nodes{
        PrefabNodeDesc{
            .stableNodeId = 7,
            .parentIndex = -1,
            .positionY = 0.5F,
            .meshId = meshId,
            .materialId = materialId,
            .visible = true,
        },
    };
    const PrefabPayloadDesc desc{.nodes = nodes};
    auto payload = writePrefabPayloadBytes(desc);
    ASSERT_TRUE(payload.has_value()) << (payload ? "" : payload.error().message);

    std::vector<PrefabNodeView> storage;
    auto view = parsePrefabPayload(*payload, storage);
    ASSERT_TRUE(view.has_value()) << (view ? "" : view.error().message);
    EXPECT_EQ(view->schemaVersion, PrefabWire::SchemaVersion);
    ASSERT_EQ(view->nodes.size(), 1U);
    EXPECT_EQ(view->nodes[0].stableNodeId, 7U);
    EXPECT_EQ(view->nodes[0].parentIndex, -1);
    EXPECT_FLOAT_EQ(view->nodes[0].positionY, 0.5F);
    EXPECT_TRUE(view->nodes[0].hasMesh);
    EXPECT_TRUE(view->nodes[0].hasMaterial);
    EXPECT_EQ(view->nodes[0].meshId, meshId);
    EXPECT_EQ(view->nodes[0].materialId, materialId);
    EXPECT_TRUE(view->nodes[0].visible);

    const auto prefabId = *Core::AssetId::fromBytes(idBytes(0x19));
    auto cooked = writeCookedPrefabAsset(prefabId, desc);
    ASSERT_TRUE(cooked.has_value()) << (cooked ? "" : cooked.error().message);
    auto cookedView = parseCookedAssetView(*cooked);
    ASSERT_TRUE(cookedView.has_value()) << (cookedView ? "" : cookedView.error().message);
    EXPECT_EQ(cookedView->header().assetKind, AssetKind::Prefab);
    ASSERT_EQ(cookedView->header().dependencyCount, 2U);
    const auto dep0 = cookedView->dependency(0);
    const auto dep1 = cookedView->dependency(1);
    ASSERT_TRUE(dep0.has_value());
    ASSERT_TRUE(dep1.has_value());
    EXPECT_EQ(dep0->assetId, materialId);
    EXPECT_EQ(dep0->expectedKind, AssetKind::Material);
    EXPECT_EQ(dep1->assetId, meshId);
    EXPECT_EQ(dep1->expectedKind, AssetKind::StaticMesh);
    ASSERT_TRUE(verifyCookedAssetContentHash(*cookedView).has_value());
}

TEST(PrefabPayloadTests, RejectsForwardParentAndMeshWithoutMaterial)
{
    const std::array badParent{
        PrefabNodeDesc{.stableNodeId = 1, .parentIndex = 1},
        PrefabNodeDesc{.stableNodeId = 2, .parentIndex = -1},
    };
    EXPECT_FALSE(writePrefabPayloadBytes(PrefabPayloadDesc{.nodes = badParent}));

    const auto meshOnlyId = *Core::AssetId::fromBytes(idBytes(0x13));
    const std::array meshOnly{
        PrefabNodeDesc{
            .stableNodeId = 1,
            .parentIndex = -1,
            .meshId = meshOnlyId,
        },
    };
    EXPECT_FALSE(writePrefabPayloadBytes(PrefabPayloadDesc{.nodes = meshOnly}));
}

TEST(PrefabPayloadTests, HierarchyParentMustPrecedeChild)
{
    const std::array nodes{
        PrefabNodeDesc{.stableNodeId = 1, .parentIndex = -1},
        PrefabNodeDesc{.stableNodeId = 2, .parentIndex = 0, .positionX = 1.0F},
    };
    auto payload = writePrefabPayloadBytes(PrefabPayloadDesc{.nodes = nodes});
    ASSERT_TRUE(payload.has_value());
    std::vector<PrefabNodeView> storage;
    auto view = parsePrefabPayload(*payload, storage);
    ASSERT_TRUE(view.has_value());
    ASSERT_EQ(view->nodes.size(), 2U);
    EXPECT_EQ(view->nodes[1].parentIndex, 0);
}

} // namespace
} // namespace Tina::AssetFormat
