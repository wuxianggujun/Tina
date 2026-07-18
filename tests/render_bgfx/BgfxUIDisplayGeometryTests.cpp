#include "BgfxUIDisplayGeometry.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <span>
#include <utility>

namespace Tina::Render::Bgfx {
namespace {

[[nodiscard]] UIDisplayListBuilder createBuilder(u32 commandCapacity = 2)
{
    auto result = UIDisplayListBuilder::Create({
        .commandCount = commandCapacity,
        .clipCount = 0,
        .batchCount = commandCapacity,
    });
    EXPECT_TRUE(result.has_value());
    return std::move(*result);
}

[[nodiscard]] Core::Result<UIDisplayListView>
buildDisplayList(UIDisplayListBuilder& builder, std::span<const UISolidQuadInput> quads)
{
    if (auto status = builder.beginFrame(); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    for (const UISolidQuadInput& quad : quads)
    {
        if (auto status = builder.addSolidQuad(quad); !status)
        {
            return Core::failure(std::move(status.error()));
        }
    }
    return builder.commit();
}

[[nodiscard]] constexpr std::array<UISolidQuadInput, 2> twoQuads() noexcept
{
    return {
        UISolidQuadInput{
            .paintOrdinal = 3,
            .bounds = {10, 20, 30, 40},
            .color = {.red = 128, .green = 64, .blue = 1, .alpha = 128},
        },
        UISolidQuadInput{
            .paintOrdinal = 9,
            .bounds = {-5, -7, 2, 3},
            .color = {.red = 10, .green = 20, .blue = 30, .alpha = 255},
        },
    };
}

void expectVertex(const BgfxUIDisplayVertex& vertex, float x, float y, u32 abgr)
{
    EXPECT_FLOAT_EQ(vertex.x, x);
    EXPECT_FLOAT_EQ(vertex.y, y);
    EXPECT_EQ(vertex.abgr, abgr);
}

TEST(BgfxUIDisplayGeometryTest, EmptyDisplayListHasZeroRequirementsAndWritesNothing)
{
    constexpr BgfxUIDisplayVertex VertexSentinel{123.0F, 456.0F, 0xDEADBEEFU};
    constexpr u32 IndexSentinel = 0xA5A5A5A5U;
    std::array<BgfxUIDisplayVertex, 1> vertices{VertexSentinel};
    std::array<u32, 1> indices{IndexSentinel};

    auto requirements = checkedGeometryRequirements({});
    ASSERT_TRUE(requirements.has_value());
    EXPECT_EQ(requirements->vertexCount, 0U);
    EXPECT_EQ(requirements->indexCount, 0U);

    auto written = writeGeometry({}, vertices, indices);
    ASSERT_TRUE(written.has_value());
    EXPECT_EQ(written->vertexCount, 0U);
    EXPECT_EQ(written->indexCount, 0U);
    expectVertex(vertices.front(), VertexSentinel.x, VertexSentinel.y, VertexSentinel.abgr);
    EXPECT_EQ(indices.front(), IndexSentinel);
}

TEST(BgfxUIDisplayGeometryTest, ExpandsTwoSolidQuadsInPaintOrderWithAbsoluteIndicesAndAbgrColor)
{
    auto builder = createBuilder();
    constexpr auto Quads = twoQuads();
    auto displayList = buildDisplayList(builder, Quads);
    ASSERT_TRUE(displayList.has_value());

    auto requirements = checkedGeometryRequirements(*displayList);
    ASSERT_TRUE(requirements.has_value());
    EXPECT_EQ(requirements->vertexCount, 8U);
    EXPECT_EQ(requirements->indexCount, 12U);

    constexpr BgfxUIDisplayVertex VertexSentinel{321.0F, 654.0F, 0xCAFEBABEU};
    constexpr u32 IndexSentinel = 0xEFEFEFEFU;
    std::array<BgfxUIDisplayVertex, 9> vertices;
    std::array<u32, 13> indices;
    vertices.fill(VertexSentinel);
    indices.fill(IndexSentinel);

    auto written = writeGeometry(*displayList, vertices, indices);
    ASSERT_TRUE(written.has_value());
    EXPECT_EQ(written->vertexCount, 8U);
    EXPECT_EQ(written->indexCount, 12U);

    constexpr u32 FirstAbgr = 0x80014080U;
    expectVertex(vertices[0], 10.0F, 20.0F, FirstAbgr);
    expectVertex(vertices[1], 40.0F, 20.0F, FirstAbgr);
    expectVertex(vertices[2], 40.0F, 60.0F, FirstAbgr);
    expectVertex(vertices[3], 10.0F, 60.0F, FirstAbgr);

    constexpr u32 SecondAbgr = 0xFF1E140AU;
    expectVertex(vertices[4], -5.0F, -7.0F, SecondAbgr);
    expectVertex(vertices[5], -3.0F, -7.0F, SecondAbgr);
    expectVertex(vertices[6], -3.0F, -4.0F, SecondAbgr);
    expectVertex(vertices[7], -5.0F, -4.0F, SecondAbgr);
    expectVertex(vertices[8], VertexSentinel.x, VertexSentinel.y, VertexSentinel.abgr);

    constexpr std::array<u32, 12> ExpectedIndices{0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7};
    EXPECT_TRUE(std::ranges::equal(std::span<const u32>{indices}.first(ExpectedIndices.size()), ExpectedIndices));
    EXPECT_EQ(indices.back(), IndexSentinel);
}

TEST(BgfxUIDisplayGeometryTest, InsufficientOutputCapacityLeavesBothBuffersUntouched)
{
    auto builder = createBuilder();
    constexpr auto Quads = twoQuads();
    auto displayList = buildDisplayList(builder, Quads);
    ASSERT_TRUE(displayList.has_value());

    constexpr BgfxUIDisplayVertex VertexSentinel{11.0F, 22.0F, 0x12345678U};
    constexpr u32 IndexSentinel = 0x87654321U;
    std::array<BgfxUIDisplayVertex, 8> vertices;
    std::array<u32, 12> indices;
    vertices.fill(VertexSentinel);
    indices.fill(IndexSentinel);

    auto shortVertices = writeGeometry(*displayList, std::span{vertices}.first(7), indices);
    ASSERT_FALSE(shortVertices.has_value());
    EXPECT_EQ(shortVertices.error().code, Core::CoreErrorCode::CapacityExceeded);
    EXPECT_TRUE(std::ranges::all_of(vertices, [&](const BgfxUIDisplayVertex& vertex) {
        return vertex.x == VertexSentinel.x && vertex.y == VertexSentinel.y && vertex.abgr == VertexSentinel.abgr;
    }));
    EXPECT_TRUE(std::ranges::all_of(indices, [](u32 index) { return index == IndexSentinel; }));

    auto shortIndices = writeGeometry(*displayList, vertices, std::span{indices}.first(11));
    ASSERT_FALSE(shortIndices.has_value());
    EXPECT_EQ(shortIndices.error().code, Core::CoreErrorCode::CapacityExceeded);
    EXPECT_TRUE(std::ranges::all_of(vertices, [&](const BgfxUIDisplayVertex& vertex) {
        return vertex.x == VertexSentinel.x && vertex.y == VertexSentinel.y && vertex.abgr == VertexSentinel.abgr;
    }));
    EXPECT_TRUE(std::ranges::all_of(indices, [](u32 index) { return index == IndexSentinel; }));
}

TEST(BgfxUIDisplayGeometryTest, UnsupportedCommandKindFailsBeforeWritingAnyGeometry)
{
    auto builder = createBuilder();
    constexpr auto Quads = twoQuads();
    auto displayList = buildDisplayList(builder, Quads);
    ASSERT_TRUE(displayList.has_value());
    auto* mutableCommands = const_cast<UIDrawCommand*>(displayList->commands().data());
    mutableCommands[1].kind = static_cast<UIDrawCommandKind>(0xFFU);

    constexpr BgfxUIDisplayVertex VertexSentinel{33.0F, 44.0F, 0x0BADF00DU};
    constexpr u32 IndexSentinel = 0xF00DBAADU;
    std::array<BgfxUIDisplayVertex, 8> vertices;
    std::array<u32, 12> indices;
    vertices.fill(VertexSentinel);
    indices.fill(IndexSentinel);

    auto requirements = checkedGeometryRequirements(*displayList);
    ASSERT_FALSE(requirements.has_value());
    EXPECT_EQ(requirements.error().code, Core::CoreErrorCode::Unsupported);
    auto written = writeGeometry(*displayList, vertices, indices);
    ASSERT_FALSE(written.has_value());
    EXPECT_EQ(written.error().code, Core::CoreErrorCode::Unsupported);
    EXPECT_TRUE(std::ranges::all_of(vertices, [&](const BgfxUIDisplayVertex& vertex) {
        return vertex.x == VertexSentinel.x && vertex.y == VertexSentinel.y && vertex.abgr == VertexSentinel.abgr;
    }));
    EXPECT_TRUE(std::ranges::all_of(indices, [](u32 index) { return index == IndexSentinel; }));
}

TEST(BgfxUIDisplayGeometryTest, ReusesCallerOwnedStorageForThreeHundredWrites)
{
    auto builder = createBuilder();
    constexpr auto Quads = twoQuads();
    auto displayList = buildDisplayList(builder, Quads);
    ASSERT_TRUE(displayList.has_value());
    std::array<BgfxUIDisplayVertex, 8> vertices{};
    std::array<u32, 12> indices{};
    BgfxUIDisplayVertex* const fixedVertices = vertices.data();
    u32* const fixedIndices = indices.data();

    for (u32 iteration = 0; iteration < 300; ++iteration)
    {
        auto written = writeGeometry(*displayList, vertices, indices);
        ASSERT_TRUE(written.has_value());
        EXPECT_EQ(written->vertexCount, vertices.size());
        EXPECT_EQ(written->indexCount, indices.size());
        EXPECT_EQ(vertices.data(), fixedVertices);
        EXPECT_EQ(indices.data(), fixedIndices);
        EXPECT_EQ(vertices[0].abgr, 0x80014080U);
        EXPECT_EQ(indices[11], 7U);
    }
}

} // namespace
} // namespace Tina::Render::Bgfx
