#include <tina/asset_format/AssetFormat.hpp>
#include <tina/asset_format/AssetFormatErrors.hpp>
#include <tina/asset_format/TileMapChunkPayload.hpp>
#include <tina/core/id/AssetId.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <vector>

namespace Tina::AssetFormat {
namespace {

[[nodiscard]] Core::AssetId makeId(Core::u8 seed)
{
    Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(seed);
    bytes[15] = static_cast<std::byte>(seed ^ 0x5AU);
    return *Core::AssetId::fromBytes(bytes);
}

void putU16(std::vector<std::byte>& bytes, Core::usize offset, Core::u16 value)
{
    bytes[offset] = static_cast<std::byte>(value & 0xFFU);
    bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xFFU);
}

void putU32(std::vector<std::byte>& bytes, Core::usize offset, Core::u32 value)
{
    for (Core::usize index = 0; index < sizeof(value); ++index)
    {
        bytes[offset + index] = static_cast<std::byte>((value >> (index * 8U)) & 0xFFU);
    }
}

void expectParseError(const std::vector<std::byte>& bytes, Core::ErrorCode expectedCode)
{
    auto parsed = parseTileMapChunkPayload(bytes);
    ASSERT_FALSE(parsed.has_value());
    EXPECT_EQ(parsed.error().code, expectedCode) << parsed.error().message;
}

[[nodiscard]] TileMapChunkPayloadDesc makeDesc(Core::AssetId parent, std::span<const Core::u16> cells)
{
    return TileMapChunkPayloadDesc{
        .parentTileMapId = parent,
        .layerId = 17U,
        .chunkX = 4U,
        .chunkY = 9U,
        .widthCells = 3U,
        .heightCells = 2U,
        .cells = cells,
    };
}

TEST(TileMapChunkPayloadTests, PayloadRoundTripPreservesIdentityLayoutAndRowMajorCells)
{
    const auto parent = makeId(0x11U);
    const std::array<Core::u16, 6> cells{0U, 3U, 7U, 0U, 9U, 0U};

    auto bytes = writeTileMapChunkPayloadBytes(makeDesc(parent, cells));
    ASSERT_TRUE(bytes.has_value()) << bytes.error().message;
    ASSERT_EQ(bytes->size(), TileMapChunkWire::HeaderBytes + cells.size() * sizeof(Core::u16));

    auto view = parseTileMapChunkPayload(*bytes);
    ASSERT_TRUE(view.has_value()) << view.error().message;
    EXPECT_EQ(view->schemaVersion, TileMapChunkWire::SchemaVersion);
    EXPECT_EQ(view->parentTileMapId, parent);
    EXPECT_EQ(view->layerId, 17U);
    EXPECT_EQ(view->chunkX, 4U);
    EXPECT_EQ(view->chunkY, 9U);
    EXPECT_EQ(view->widthCells, 3U);
    EXPECT_EQ(view->heightCells, 2U);
    EXPECT_EQ(view->cellCount, 6U);
    EXPECT_EQ(view->nonEmptyCount, 3U);
    EXPECT_EQ(view->cellAt(0U, 0U), 0U);
    EXPECT_EQ(view->cellAt(2U, 0U), 7U);
    EXPECT_EQ(view->cellAt(1U, 1U), 9U);
    EXPECT_FALSE(view->cellAt(3U, 0U).has_value());
}

TEST(TileMapChunkPayloadTests, EdgeChunkAndCompleteCookedAssetRoundTrip)
{
    const auto parent = makeId(0x21U);
    const auto chunk = makeId(0x22U);
    const std::array<Core::u16, 3> cells{2U, 0U, 5U};
    const TileMapChunkPayloadDesc edge{
        .parentTileMapId = parent,
        .layerId = 3U,
        .chunkX = 15U,
        .chunkY = 8U,
        .widthCells = 1U,
        .heightCells = 3U,
        .cells = cells,
    };

    auto cooked = writeCookedTileMapChunkAsset(chunk, edge);
    ASSERT_TRUE(cooked.has_value()) << cooked.error().message;
    auto asset = parseCookedAssetView(*cooked);
    ASSERT_TRUE(asset.has_value()) << asset.error().message;
    EXPECT_EQ(asset->header().assetKind, AssetKind::TileMapChunk);
    EXPECT_EQ(asset->header().assetTypeVersion, TileMapChunkWire::SchemaVersion);
    EXPECT_EQ(asset->header().assetId, chunk);
    EXPECT_EQ(asset->header().dependencyCount, 0U);
    ASSERT_TRUE(verifyCookedAssetContentHash(*asset).has_value());

    auto view = parseTileMapChunkPayload(asset->payload());
    ASSERT_TRUE(view.has_value()) << view.error().message;
    EXPECT_EQ(view->parentTileMapId, parent);
    EXPECT_EQ(view->widthCells, 1U);
    EXPECT_EQ(view->heightCells, 3U);
    EXPECT_EQ(view->nonEmptyCount, 2U);
    EXPECT_EQ(view->cellAt(0U, 2U), 5U);
}

TEST(TileMapChunkPayloadTests, WriterRejectsBadIdentityDimensionsAndCellCount)
{
    const auto parent = makeId(0x31U);
    const std::array<Core::u16, 6> cells{0U, 1U, 0U, 1U, 0U, 1U};
    auto desc = makeDesc(parent, cells);

    desc.parentTileMapId = {};
    EXPECT_FALSE(writeTileMapChunkPayloadBytes(desc).has_value());
    desc.parentTileMapId = parent;
    desc.layerId = 0U;
    EXPECT_FALSE(writeTileMapChunkPayloadBytes(desc).has_value());
    desc.layerId = 17U;
    desc.widthCells = 0U;
    EXPECT_FALSE(writeTileMapChunkPayloadBytes(desc).has_value());
    desc.widthCells = 3U;
    desc.cells = std::span<const Core::u16>{cells}.first(5U);
    EXPECT_FALSE(writeTileMapChunkPayloadBytes(desc).has_value());

    std::array<Core::u16, TileMapChunkWire::MaxDimension + 1U> tooWide{};
    desc.widthCells = TileMapChunkWire::MaxDimension + 1U;
    desc.heightCells = 1U;
    desc.cells = tooWide;
    EXPECT_FALSE(writeTileMapChunkPayloadBytes(desc).has_value());
    EXPECT_FALSE(writeCookedTileMapChunkAsset(parent, makeDesc(parent, cells)).has_value());

    const std::array<Core::u16, 6> emptyCells{};
    EXPECT_FALSE(writeTileMapChunkPayloadBytes(makeDesc(parent, emptyCells)).has_value());
}

TEST(TileMapChunkPayloadTests, ParserRejectsBadParentLayerDimensionsAndCounts)
{
    const auto parent = makeId(0x41U);
    const std::array<Core::u16, 6> cells{0U, 1U, 0U, 2U, 0U, 3U};
    auto written = writeTileMapChunkPayloadBytes(makeDesc(parent, cells));
    ASSERT_TRUE(written.has_value());

    auto badParent = *written;
    std::fill_n(badParent.begin() + 4U, Core::AssetId::Bytes{}.size(), std::byte{0});
    expectParseError(badParent, AssetFormatErrorCode::InvalidIdentity);

    auto badLayer = *written;
    putU32(badLayer, 20U, 0U);
    expectParseError(badLayer, AssetFormatErrorCode::InvalidIdentity);

    auto badDimension = *written;
    putU16(badDimension, 32U, 0U);
    expectParseError(badDimension, AssetFormatErrorCode::InvalidLayout);

    auto badCellCount = *written;
    putU32(badCellCount, 36U, 5U);
    expectParseError(badCellCount, AssetFormatErrorCode::InvalidLayout);

    auto badNonEmptyCount = *written;
    putU32(badNonEmptyCount, 40U, 2U);
    expectParseError(badNonEmptyCount, AssetFormatErrorCode::InvalidLayout);

    auto emptyChunk = *written;
    putU32(emptyChunk, 40U, 0U);
    std::fill(emptyChunk.begin() + TileMapChunkWire::HeaderBytes, emptyChunk.end(), std::byte{0});
    expectParseError(emptyChunk, AssetFormatErrorCode::InvalidLayout);
}

TEST(TileMapChunkPayloadTests, ParserRejectsSchemaReservedTrailingAndTruncatedPayloads)
{
    const auto parent = makeId(0x51U);
    const std::array<Core::u16, 6> cells{0U, 1U, 0U, 2U, 0U, 3U};
    auto written = writeTileMapChunkPayloadBytes(makeDesc(parent, cells));
    ASSERT_TRUE(written.has_value());

    auto badSchema = *written;
    putU16(badSchema, 0U, TileMapChunkWire::SchemaVersion + 1U);
    expectParseError(badSchema, AssetFormatErrorCode::UnsupportedSchema);

    auto badFlags = *written;
    putU16(badFlags, 2U, 1U);
    expectParseError(badFlags, AssetFormatErrorCode::UnsupportedValue);

    auto badReserved = *written;
    putU32(badReserved, 44U, 1U);
    expectParseError(badReserved, AssetFormatErrorCode::UnsupportedValue);

    auto trailing = *written;
    trailing.push_back(std::byte{0});
    expectParseError(trailing, AssetFormatErrorCode::InvalidLayout);

    auto truncated = *written;
    truncated.pop_back();
    expectParseError(truncated, AssetFormatErrorCode::InvalidLayout);
}

} // namespace
} // namespace Tina::AssetFormat
