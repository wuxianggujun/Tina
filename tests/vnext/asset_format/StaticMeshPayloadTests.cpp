#include <tina/asset_format/AssetFormat.hpp>
#include <tina/asset_format/AssetFormatErrors.hpp>
#include <tina/asset_format/StaticMeshPayload.hpp>
#include <tina/core/id/AssetId.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <vector>

namespace Tina::AssetFormat {
namespace {

[[nodiscard]] Core::AssetId::Bytes idBytes(Core::u8 seed)
{
    Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(seed);
    bytes[15] = static_cast<std::byte>(seed ^ 0x3DU);
    return bytes;
}

TEST(StaticMeshPayloadTests, CanonicalCubeRoundTrip)
{
    std::array<StaticMeshSubmeshDesc, 1> submeshes{};
    std::array<float, 24 * 8> vertices{};
    std::array<Core::u16, 36> indices{};
    const StaticMeshPayloadDesc desc =
        makeCanonicalUnitCubeMeshDesc(submeshes, vertices, indices);
    ASSERT_EQ(desc.vertices.size(), 24U * 8U);
    ASSERT_EQ(desc.indices.size(), 36U);

    auto written = writeStaticMeshPayloadBytes(desc);
    ASSERT_TRUE(written.has_value()) << (written ? "" : written.error().message);

    auto view = parseStaticMeshPayload(*written);
    ASSERT_TRUE(view.has_value()) << (view ? "" : view.error().message);
    EXPECT_EQ(view->schemaVersion, StaticMeshWire::SchemaVersion);
    EXPECT_EQ(view->vertexLayout, StaticMeshVertexLayout::P3N3UV2);
    EXPECT_EQ(view->indexType, StaticMeshIndexType::U16);
    EXPECT_EQ(view->vertexCount, 24U);
    EXPECT_EQ(view->indexCount, 36U);
    EXPECT_EQ(view->submeshCount, 1U);
    EXPECT_EQ(view->submeshes[0].indexCount, 36U);
    EXPECT_FLOAT_EQ(view->boundsRadius, std::sqrt(3.0F));
    EXPECT_EQ(view->indices[0], 0U);
    EXPECT_EQ(view->indices[2], 2U);
}

TEST(StaticMeshPayloadTests, CookedStaticMeshRoundTrip)
{
    const auto meshId = *Core::AssetId::fromBytes(idBytes(0x31));
    std::array<StaticMeshSubmeshDesc, 1> submeshes{};
    std::array<float, 24 * 8> vertices{};
    std::array<Core::u16, 36> indices{};
    const StaticMeshPayloadDesc desc =
        makeCanonicalUnitCubeMeshDesc(submeshes, vertices, indices);

    auto cooked = writeCookedStaticMeshAsset(meshId, desc);
    ASSERT_TRUE(cooked.has_value()) << (cooked ? "" : cooked.error().message);

    auto asset = parseCookedAssetView(*cooked);
    ASSERT_TRUE(asset.has_value()) << (asset ? "" : asset.error().message);
    EXPECT_EQ(asset->header().assetKind, AssetKind::StaticMesh);
    EXPECT_EQ(asset->header().assetId, meshId);

    auto view = parseStaticMeshPayload(asset->payload());
    ASSERT_TRUE(view.has_value()) << (view ? "" : view.error().message);
    EXPECT_EQ(view->vertexCount, 24U);
    EXPECT_EQ(view->indexCount, 36U);
    ASSERT_TRUE(verifyCookedAssetContentHash(*asset).has_value());
}

TEST(StaticMeshPayloadTests, RejectsBadIndexAndEmptyMesh)
{
    std::array<StaticMeshSubmeshDesc, 1> submeshes{
        StaticMeshSubmeshDesc{.firstIndex = 0, .indexCount = 3, .materialSlot = 0, .reserved = 0}};
    std::array<float, 8> vertices{0, 0, 0, 0, 1, 0, 0, 0};
    std::array<Core::u16, 3> badIndices{0, 1, 2}; // index 1/2 out of range for 1 vertex

    auto bad = writeStaticMeshPayloadBytes(StaticMeshPayloadDesc{
        .vertexLayout = StaticMeshVertexLayout::P3N3UV2,
        .indexType = StaticMeshIndexType::U16,
        .boundsRadius = 1.0F,
        .submeshes = submeshes,
        .vertices = vertices,
        .indices = badIndices,
    });
    ASSERT_FALSE(bad.has_value());

    auto empty = writeStaticMeshPayloadBytes(StaticMeshPayloadDesc{
        .boundsRadius = 1.0F,
    });
    ASSERT_FALSE(empty.has_value());
}

TEST(StaticMeshPayloadTests, TruncatedPayloadFails)
{
    std::vector<std::byte> truncated(16, std::byte{0});
    auto view = parseStaticMeshPayload(truncated);
    ASSERT_FALSE(view.has_value());
    EXPECT_EQ(view.error().code, AssetFormatErrorCode::InvalidLayout);
}

} // namespace
} // namespace Tina::AssetFormat
