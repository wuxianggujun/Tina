#include <tina/asset_format/AssetFormat.hpp>
#include <tina/asset_format/TileMapChunkPayload.hpp>
#include <tina/asset_format/TileMapPayload.hpp>
#include <tina/editor/EditorErrors.hpp>
#include <tina/editor/TileMapAuthoringDocument.hpp>

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

[[nodiscard]] TileMapAuthoringDesc makeMap()
{
    return TileMapAuthoringDesc{
        .tileMapId = assetId(0x51U),
        .tilesetId = assetId(0x52U),
        .widthCells = 4,
        .heightCells = 4,
        .cellSizeMeters = 0.5F,
        .chunkSizeCells = 2,
        .layers = {
            TileMapAuthoringLayer{
                .stableLayerId = 7,
                .kind = AssetFormat::TileMapLayerKind::Tile,
                .name = "Ground",
                .chunks = {
                    TileMapAuthoringChunk{
                        .chunkX = 0,
                        .chunkY = 0,
                        .cells = {1, 0, 0, 2},
                    },
                },
            },
            TileMapAuthoringLayer{
                .stableLayerId = 9,
                .kind = AssetFormat::TileMapLayerKind::Object,
                .name = "Gameplay",
                .objects = {
                    TileMapAuthoringObject{
                        .stableObjectId = 11,
                        .kind = AssetFormat::TileMapObjectKind::Point,
                        .name = "Spawn",
                        .x = 1.0F,
                        .y = 1.5F,
                    },
                },
            },
        },
    };
}

[[nodiscard]] TileMapAuthoringDocument createDocument()
{
    auto document = TileMapAuthoringDocument::Create(makeMap());
    EXPECT_TRUE(document) << (document ? "" : document.error().message);
    return std::move(*document);
}

TEST(TileMapAuthoringDocumentTests, OwnsCanonicalRootChunksAndCookPreview)
{
    auto document = createDocument();
    EXPECT_EQ(document.layerCount(), 2U);
    EXPECT_EQ(document.chunkCount(), 1U);
    EXPECT_EQ(document.nonEmptyCellCount(), 2U);

    auto root = AssetFormat::parseTileMapPayload(document.rootPayloadBytes());
    ASSERT_TRUE(root);
    EXPECT_EQ(root->schemaVersion, AssetFormat::TileMapWire::SchemaVersion);
    ASSERT_EQ(root->layerCount, 2U);

    const auto chunkPayload = document.chunkPayloadAt(0);
    ASSERT_TRUE(chunkPayload);
    auto expectedId = AssetFormat::deriveTileMapChunkAssetId(assetId(0x51U), 7, 0, 0);
    ASSERT_TRUE(expectedId);
    EXPECT_EQ(chunkPayload->assetId, *expectedId);
    auto chunk = AssetFormat::parseTileMapChunkPayload(chunkPayload->payloadBytes);
    ASSERT_TRUE(chunk);
    EXPECT_EQ(chunk->nonEmptyCount, 2U);

    auto preview = document.cookPreview();
    ASSERT_TRUE(preview);
    ASSERT_EQ(preview->artifacts.size(), 2U);
    EXPECT_LT(preview->artifacts[0].assetId, preview->artifacts[1].assetId);
    for (const auto& artifact : preview->artifacts)
    {
        EXPECT_FALSE(artifact.path.view().empty());
        auto cooked = AssetFormat::parseCookedAssetView(artifact.cookedBytes);
        ASSERT_TRUE(cooked);
        EXPECT_EQ(cooked->header().assetId, artifact.assetId);
        EXPECT_EQ(cooked->header().assetKind, artifact.assetKind);
    }
}

TEST(TileMapAuthoringDocumentTests, PaintEraseUndoRedoOwnCompleteChunkRevisions)
{
    auto document = createDocument();
    const auto baselineRevision = document.revision();
    ASSERT_TRUE(document.paintCell(7, 3, 3, 4));
    EXPECT_EQ(document.chunkCount(), 2U);
    EXPECT_EQ(document.nonEmptyCellCount(), 3U);
    EXPECT_EQ(document.revision(), baselineRevision + 1U);

    ASSERT_TRUE(document.undo());
    EXPECT_EQ(document.chunkCount(), 1U);
    EXPECT_EQ(document.nonEmptyCellCount(), 2U);
    ASSERT_TRUE(document.redo());
    EXPECT_EQ(document.chunkCount(), 2U);

    ASSERT_TRUE(document.paintCell(7, 3, 3, 0));
    EXPECT_EQ(document.chunkCount(), 1U);
    EXPECT_EQ(document.nonEmptyCellCount(), 2U);
    EXPECT_FALSE(document.canRedo());
}

TEST(TileMapAuthoringDocumentTests, FailedBrushBatchPreservesCurrentAndRedoBranch)
{
    auto document = createDocument();
    ASSERT_TRUE(document.paintCell(7, 3, 3, 1));
    ASSERT_TRUE(document.undo());
    ASSERT_TRUE(document.canRedo());
    const auto beforeRevision = document.revision();
    const auto beforeRoot = std::vector(document.rootPayloadBytes().begin(),
                                        document.rootPayloadBytes().end());
    const std::array duplicateEdits{
        TileMapAuthoringCellEdit{.x = 1, .y = 1, .localTileId = 2},
        TileMapAuthoringCellEdit{.x = 1, .y = 1, .localTileId = 3},
    };
    const auto status = document.setCells(7, duplicateEdits);
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, EditorErrorCode::InvalidAuthoringOperation);
    EXPECT_EQ(document.revision(), beforeRevision);
    EXPECT_EQ(std::vector(document.rootPayloadBytes().begin(),
                          document.rootPayloadBytes().end()),
              beforeRoot);
    EXPECT_TRUE(document.canRedo());
}

TEST(TileMapAuthoringDocumentTests, LayerObjectAndPayloadFamilyOperationsAreTransactional)
{
    auto source = createDocument();
    ASSERT_TRUE(source.addTileLayer(12, "Decoration"));
    ASSERT_TRUE(source.addObjectLayer(13, "Triggers"));
    ASSERT_TRUE(source.upsertObject(
        13, TileMapAuthoringObject{
                .stableObjectId = 20,
                .kind = AssetFormat::TileMapObjectKind::Rectangle,
                .name = "Exit",
                .x = 2.0F,
                .y = 1.0F,
                .width = 1.0F,
                .height = 2.0F,
            }));
    ASSERT_TRUE(source.setLayerVisibility(12, false));

    std::vector<TileMapAuthoringChunkSource> chunkSources;
    for (Core::usize index = 0; index < source.chunkCount(); ++index)
    {
        const auto chunk = source.chunkPayloadAt(index);
        ASSERT_TRUE(chunk);
        chunkSources.push_back({.assetId = chunk->assetId, .payloadBytes = chunk->payloadBytes});
    }
    auto destination = createDocument();
    ASSERT_TRUE(destination.paintCell(7, 3, 3, 1));
    ASSERT_TRUE(destination.loadPayloadFamily(
        source.tileMapId(), source.tilesetId(), source.rootPayloadBytes(), chunkSources));
    EXPECT_EQ(destination.layerCount(), 4U);
    EXPECT_EQ(destination.chunkCount(), source.chunkCount());
    EXPECT_FALSE(destination.canUndo());
    EXPECT_FALSE(destination.canRedo());
    auto authored = destination.snapshot();
    ASSERT_TRUE(authored);
    ASSERT_EQ(authored->layers.size(), 4U);
    EXPECT_FALSE(authored->layers[2].visible);
    ASSERT_EQ(authored->layers[3].objects.size(), 1U);
    EXPECT_EQ(authored->layers[3].objects[0].stableObjectId, 20U);
}

TEST(TileMapAuthoringDocumentTests, RejectsInvalidConfigurationAndCapacityWithoutPublishing)
{
    auto invalid = TileMapAuthoringDocument::Create(
        makeMap(), TileMapAuthoringDocumentConfig{.historyEntryCapacity = 1});
    ASSERT_FALSE(invalid);
    EXPECT_EQ(invalid.error().code, EditorErrorCode::InvalidConfiguration);

    auto document = TileMapAuthoringDocument::Create(
        makeMap(), TileMapAuthoringDocumentConfig{
                       .layerCapacity = 2,
                       .objectCapacity = 1,
                       .chunkCapacity = 1,
                       .historyEntryCapacity = 4,
                       .historyByteCapacity = 4096,
                   });
    ASSERT_TRUE(document);
    const auto beforeRevision = document->revision();
    const auto status = document->paintCell(7, 3, 3, 1);
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, EditorErrorCode::DocumentCapacityExceeded);
    EXPECT_EQ(document->revision(), beforeRevision);
    EXPECT_EQ(document->chunkCount(), 1U);
}

} // namespace
} // namespace Tina::Editor
