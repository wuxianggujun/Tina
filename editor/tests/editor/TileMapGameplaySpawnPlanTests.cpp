#include <tina/editor/EditorErrors.hpp>
#include <tina/editor/TileMapGameplaySpawnPlan.hpp>

#include <gtest/gtest.h>

#include <array>
#include <vector>

namespace Tina::Editor {
namespace {

[[nodiscard]] Core::AssetId assetId(Core::u8 marker)
{
    Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(marker);
    return *Core::AssetId::fromBytes(bytes);
}

[[nodiscard]] TileMapAuthoringDesc makeTileMap()
{
    return TileMapAuthoringDesc{
        .tileMapId = assetId(0x61U),
        .tilesetId = assetId(0x62U),
        .widthCells = 4,
        .heightCells = 4,
        .layers = {
            TileMapAuthoringLayer{
                .stableLayerId = 7,
                .kind = AssetFormat::TileMapLayerKind::Object,
                .name = "Gameplay",
                .objects = {
                    TileMapAuthoringObject{
                        .stableObjectId = 30,
                        .kind = AssetFormat::TileMapObjectKind::Rectangle,
                        .name = "Crate",
                        .x = 3.0F,
                        .y = 2.0F,
                        .width = 2.0F,
                        .height = 1.0F,
                        .properties = {{.key = "archetype", .value = "crate"}},
                    },
                    TileMapAuthoringObject{
                        .stableObjectId = 10,
                        .kind = AssetFormat::TileMapObjectKind::Point,
                        .name = "Player",
                        .x = 1.0F,
                        .y = 1.5F,
                        .properties = {{.key = "archetype", .value = "player"}},
                    },
                    TileMapAuthoringObject{
                        .stableObjectId = 20,
                        .visible = false,
                        .name = "Disabled",
                        .properties = {{.key = "archetype", .value = "crate"}},
                    },
                },
            },
        },
    };
}

[[nodiscard]] TileMapAuthoringDocument createTileMap()
{
    auto document = TileMapAuthoringDocument::Create(makeTileMap());
    EXPECT_TRUE(document) << (document ? "" : document.error().message);
    return std::move(*document);
}

[[nodiscard]] World2DAuthoringDocument createWorld()
{
    auto document = World2DAuthoringDocument::Create({
        .entityCapacity = 4,
        .gameplayByteCapacity = 64,
        .historyEntryCapacity = 4,
        .historyByteCapacity = 4096,
    });
    EXPECT_TRUE(document) << (document ? "" : document.error().message);
    const std::array entity{AssetFormat::World2DEntityDesc{.stableEntityId = 1}};
    EXPECT_TRUE(document->replace({.entities = entity}));
    return std::move(*document);
}

constexpr std::array Archetypes{
    TileMapGameplayArchetypeBinding{.archetype = "crate", .gameArchetypeId = 22},
    TileMapGameplayArchetypeBinding{.archetype = "player", .gameArchetypeId = 11},
};

TEST(TileMapGameplaySpawnPlanTests, OwnsVisibleRecordsInStableObjectOrder)
{
    auto tileMap = createTileMap();
    auto plan = TileMapGameplaySpawnPlan::Build(
        tileMap, Archetypes, {.objectLayerId = 7, .recordCapacity = 3});
    ASSERT_TRUE(plan) << (plan ? "" : plan.error().message);
    ASSERT_EQ(plan->records().size(), 2U);
    EXPECT_EQ(plan->sourceDocumentRevision(), tileMap.revision());
    EXPECT_EQ(plan->records()[0].stableObjectId, 10U);
    EXPECT_EQ(plan->records()[0].gameArchetypeId, 11U);
    EXPECT_EQ(plan->records()[1].stableObjectId, 30U);
    EXPECT_EQ(plan->records()[1].gameArchetypeId, 22U);
    EXPECT_EQ(plan->records()[1].kind, AssetFormat::TileMapObjectKind::Rectangle);
    EXPECT_FLOAT_EQ(plan->records()[1].width, 2.0F);

    ASSERT_TRUE(tileMap.eraseObject(7, 10));
    ASSERT_EQ(plan->records().size(), 2U);
    EXPECT_EQ(plan->records()[0].stableObjectId, 10U);
}

TEST(TileMapGameplaySpawnPlanTests, RejectsUnknownDuplicateAndCapacityBeforePublishing)
{
    auto tileMap = createTileMap();
    auto authored = tileMap.snapshot();
    ASSERT_TRUE(authored);
    authored->layers[0].objects[0].properties[0].value = "unknown";
    ASSERT_TRUE(tileMap.replace(*authored));

    const auto unknown = TileMapGameplaySpawnPlan::Build(
        tileMap, Archetypes, {.objectLayerId = 7, .recordCapacity = 3});
    ASSERT_FALSE(unknown);
    EXPECT_EQ(unknown.error().code, EditorErrorCode::UnknownGameplayArchetype);

    auto missingPropertyMap = createTileMap();
    auto missingPropertyAuthoring = missingPropertyMap.snapshot();
    ASSERT_TRUE(missingPropertyAuthoring);
    missingPropertyAuthoring->layers[0].objects[0].properties.clear();
    ASSERT_TRUE(missingPropertyMap.replace(*missingPropertyAuthoring));
    const auto missingProperty = TileMapGameplaySpawnPlan::Build(
        missingPropertyMap, Archetypes, {.objectLayerId = 7, .recordCapacity = 3});
    ASSERT_FALSE(missingProperty);
    EXPECT_EQ(missingProperty.error().code, EditorErrorCode::UnknownGameplayArchetype);

    const std::array duplicateNames{
        TileMapGameplayArchetypeBinding{.archetype = "player", .gameArchetypeId = 1},
        TileMapGameplayArchetypeBinding{.archetype = "player", .gameArchetypeId = 2},
    };
    const auto duplicate = TileMapGameplaySpawnPlan::Build(
        createTileMap(), duplicateNames, {.objectLayerId = 7, .recordCapacity = 3});
    ASSERT_FALSE(duplicate);
    EXPECT_EQ(duplicate.error().code, EditorErrorCode::DuplicateGameplayArchetype);

    const std::array duplicateIds{
        TileMapGameplayArchetypeBinding{.archetype = "player", .gameArchetypeId = 1},
        TileMapGameplayArchetypeBinding{.archetype = "crate", .gameArchetypeId = 1},
    };
    const auto duplicateId = TileMapGameplaySpawnPlan::Build(
        createTileMap(), duplicateIds, {.objectLayerId = 7, .recordCapacity = 3});
    ASSERT_FALSE(duplicateId);
    EXPECT_EQ(duplicateId.error().code, EditorErrorCode::DuplicateGameplayArchetype);

    const auto capacity = TileMapGameplaySpawnPlan::Build(
        createTileMap(), Archetypes, {.objectLayerId = 7, .recordCapacity = 1});
    ASSERT_FALSE(capacity);
    EXPECT_EQ(capacity.error().code, EditorErrorCode::DocumentCapacityExceeded);
}

TEST(TileMapGameplaySpawnPlanTests, PublishesOneWorldRevisionAfterEncodingCompletes)
{
    auto tileMap = createTileMap();
    auto world = createWorld();
    const Core::u64 revisionBefore = world.revision();
    const Core::usize undoDepthBefore = world.undoDepth();
    auto generated = generateTileMapGameplay(
        tileMap, world, Archetypes,
        {.objectLayerId = 7, .recordCapacity = 3},
        {.gameplaySchema = 700, .gameplayVersion = 1},
        [](std::span<const TileMapGameplaySpawnRecord> records)
            -> Core::Result<std::vector<std::byte>> {
            std::vector<std::byte> bytes;
            bytes.reserve(1U + records.size() * 2U);
            bytes.push_back(static_cast<std::byte>(records.size()));
            for (const TileMapGameplaySpawnRecord& record : records)
            {
                bytes.push_back(static_cast<std::byte>(record.stableObjectId));
                bytes.push_back(static_cast<std::byte>(record.gameArchetypeId));
            }
            return bytes;
        });
    ASSERT_TRUE(generated) << (generated ? "" : generated.error().message);
    EXPECT_EQ(world.revision(), revisionBefore + 1U);
    EXPECT_EQ(world.undoDepth(), undoDepthBefore + 1U);
    EXPECT_EQ(world.gameplaySchema(), 700U);
    EXPECT_EQ(world.gameplayVersion(), 1U);
    EXPECT_EQ(world.gameplayByteCount(), 5U);

    ASSERT_TRUE(world.undo());
    EXPECT_EQ(world.gameplayByteCount(), 0U);
}

TEST(TileMapGameplaySpawnPlanTests, EncoderFailurePreservesWorldAndRedoBranch)
{
    auto tileMap = createTileMap();
    auto world = createWorld();
    const std::array existingGameplay{std::byte{1}};
    ASSERT_TRUE(world.setGameplay(1, 1, existingGameplay));
    ASSERT_TRUE(world.undo());
    ASSERT_TRUE(world.canRedo());
    const Core::u64 revisionBefore = world.revision();
    const auto bytesBefore = std::vector(world.snapshotBytes().begin(), world.snapshotBytes().end());

    const auto generated = generateTileMapGameplay(
        tileMap, world, Archetypes,
        {.objectLayerId = 7, .recordCapacity = 3},
        {.gameplaySchema = 700, .gameplayVersion = 1},
        [](std::span<const TileMapGameplaySpawnRecord>)
            -> Core::Result<std::vector<std::byte>> {
            return Core::failure(EditorErrorCode::InvalidAuthoringOperation,
                                 "game-owned encoder rejected the plan");
        });
    ASSERT_FALSE(generated);
    EXPECT_EQ(world.revision(), revisionBefore);
    EXPECT_EQ(std::vector(world.snapshotBytes().begin(), world.snapshotBytes().end()), bytesBefore);
    EXPECT_TRUE(world.canRedo());
}

} // namespace
} // namespace Tina::Editor
