#include "BgfxSprite2DGeometry.hpp"

#include <tina/core/error/Error.hpp>
#include <tina/render/FramePin.hpp>
#include <tina/render/RenderErrors.hpp>
#include <tina/render/RenderFramePacket.hpp>
#include <tina/render/RenderScene.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>

namespace Tina::Render::Bgfx {
namespace {

void countRelease(void* userData) noexcept
{
    ++(*static_cast<u32*>(userData));
}

class FrameResourceScope final {
public:
    FrameResourceScope()
    {
        EXPECT_TRUE(packet_.beginFrame(0));
    }

    [[nodiscard]] FrameResourceRef texture(u64 deviceBindingKey)
    {
        FramePin pin{FramePinKind::Custom, deviceBindingKey, &releaseCount_, &countRelease};
        auto result = packet_.intern(
            FrameResourceDescriptor{
                .kind = FrameResourceKind::Texture2D,
                .deviceBindingKey = deviceBindingKey,
            },
            std::move(pin));
        EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
        return result ? *result : FrameResourceRef{};
    }

    [[nodiscard]] FrameResourceTableView view() const noexcept
    {
        return packet_.resourceTableView();
    }

private:
    u32 releaseCount_ = 0;
    RenderFramePacket packet_{};
};

[[nodiscard]] RenderSceneBuilder makeBuilder(u32 spriteCapacity = 8)
{
    RenderSceneCapacity capacity{};
    capacity.spriteCapacity = spriteCapacity;
    auto result = RenderSceneBuilder::Create(capacity);
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return std::move(*result);
}

[[nodiscard]] RenderCamera2DInput camera() noexcept
{
    return RenderCamera2DInput{
        .stableCameraKey = 7,
        .centerX = 0.0F,
        .centerY = 0.0F,
        .rotationRadians = 0.0F,
        .worldWidth = 10.0F,
        .worldHeight = 10.0F,
        .actualPixelsPerMeter = 10.0F,
    };
}

[[nodiscard]] RenderSprite2DInput sprite(FrameResourceRef texture, u64 stableEntityKey, float centerX,
                                         i16 sortingLayer = 0, i32 orderInLayer = 0) noexcept
{
    return RenderSprite2DInput{
        .texture = texture,
        .stableEntityKey = stableEntityKey,
        .centerX = centerX,
        .centerY = 0.0F,
        .widthMeters = 2.0F,
        .heightMeters = 2.0F,
        .sortingLayer = sortingLayer,
        .orderInLayer = orderInLayer,
        .red = 128,
        .green = 64,
        .blue = 1,
        .alpha = 192,
    };
}

[[nodiscard]] Core::Result<RenderSceneView> commitScene(RenderSceneBuilder& builder,
                                                        std::span<const RenderSprite2DInput> sprites)
{
    if (auto status = builder.beginFrame(); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    auto writer = builder.writer();
    if (auto status = writer.setCamera2D(camera()); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    for (const RenderSprite2DInput& input : sprites)
    {
        if (auto status = writer.addSprite2D(input); !status)
        {
            return Core::failure(std::move(status.error()));
        }
    }
    return builder.commit();
}

void expectVertex(const BgfxSprite2DVertex& vertex, float x, float y, float u, float v, u32 abgr)
{
    EXPECT_FLOAT_EQ(vertex.positionX, x);
    EXPECT_FLOAT_EQ(vertex.positionY, y);
    EXPECT_FLOAT_EQ(vertex.textureU, u);
    EXPECT_FLOAT_EQ(vertex.textureV, v);
    EXPECT_EQ(vertex.abgr, abgr);
}

TEST(BgfxSprite2DGeometryTest, VertexLayoutUsesP2UV2AndPackedAbgr)
{
    EXPECT_TRUE(std::is_standard_layout_v<BgfxSprite2DVertex>);
    EXPECT_EQ(sizeof(BgfxSprite2DVertex), 20U);
    EXPECT_EQ(offsetof(BgfxSprite2DVertex, positionX), 0U);
    EXPECT_EQ(offsetof(BgfxSprite2DVertex, positionY), 4U);
    EXPECT_EQ(offsetof(BgfxSprite2DVertex, textureU), 8U);
    EXPECT_EQ(offsetof(BgfxSprite2DVertex, textureV), 12U);
    EXPECT_EQ(offsetof(BgfxSprite2DVertex, abgr), 16U);
}

TEST(BgfxSprite2DGeometryTest, EmptySceneNeedsNoGeometry)
{
    RenderSceneBuilder builder = makeBuilder();
    ASSERT_TRUE(builder.beginFrame());
    auto scene = builder.commit();
    ASSERT_TRUE(scene.has_value());

    auto requirements = checkedSprite2DFrame(*scene, {});
    ASSERT_TRUE(requirements.has_value());
    EXPECT_EQ(requirements->spriteCount, 0U);
    EXPECT_EQ(requirements->vertexCount, 0U);
    EXPECT_EQ(requirements->indexCount, 0U);
    EXPECT_EQ(requirements->batchCount, 0U);
}

TEST(BgfxSprite2DGeometryTest, ValidFixtureExpandsSortedSpritesInRenderOrder)
{
    FrameResourceScope resources;
    const FrameResourceRef texture = resources.texture(1);
    RenderSceneBuilder builder = makeBuilder();
    const std::array inputs{
        sprite(texture, 30, 3.0F, 2, 0),
        sprite(texture, 10, -1.0F, 1, 5),
    };
    auto scene = commitScene(builder, inputs);
    ASSERT_TRUE(scene.has_value()) << scene.error().message;
    ASSERT_EQ(scene->sprites2D().size(), 2U);
    EXPECT_EQ(scene->sprites2D()[0].stableEntityKey, 10U);
    EXPECT_EQ(scene->sprites2D()[1].stableEntityKey, 30U);

    auto requirements = checkedSprite2DFrame(*scene, resources.view());
    ASSERT_TRUE(requirements.has_value());
    EXPECT_EQ(requirements->spriteCount, 2U);
    EXPECT_EQ(requirements->vertexCount, 8U);
    EXPECT_EQ(requirements->indexCount, 12U);
    EXPECT_EQ(requirements->batchCount, 1U);

    std::array<BgfxSprite2DVertex, 8> vertices{};
    std::array<u32, 12> indices{};
    auto written = writeSprite2DGeometry(*scene, resources.view(), vertices, indices);
    ASSERT_TRUE(written.has_value());

    constexpr u32 ExpectedAbgr = 0xC0014080U;
    expectVertex(vertices[0], -2.0F, -1.0F, 0.0F, 1.0F, ExpectedAbgr);
    expectVertex(vertices[1], 0.0F, -1.0F, 1.0F, 1.0F, ExpectedAbgr);
    expectVertex(vertices[2], 0.0F, 1.0F, 1.0F, 0.0F, ExpectedAbgr);
    expectVertex(vertices[3], -2.0F, 1.0F, 0.0F, 0.0F, ExpectedAbgr);
    expectVertex(vertices[4], 2.0F, -1.0F, 0.0F, 1.0F, ExpectedAbgr);

    constexpr std::array<u32, 12> ExpectedIndices{0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7};
    EXPECT_TRUE(std::ranges::equal(indices, ExpectedIndices));
}

TEST(BgfxSprite2DGeometryTest, RotationAndScaleProduceWorldSpaceQuad)
{
    FrameResourceScope resources;
    const FrameResourceRef texture = resources.texture(1);
    RenderSceneBuilder builder = makeBuilder();
    auto rotated = sprite(texture, 1, 0.0F);
    rotated.widthMeters = 4.0F;
    rotated.heightMeters = 2.0F;
    rotated.scaleX = 0.5F;
    rotated.scaleY = 2.0F;
    rotated.rotationRadians = std::numbers::pi_v<float> * 0.5F;
    const std::array inputs{rotated};

    auto scene = commitScene(builder, inputs);
    ASSERT_TRUE(scene.has_value()) << scene.error().message;

    std::array<BgfxSprite2DVertex, 4> vertices{};
    std::array<u32, 6> indices{};
    auto written = writeSprite2DGeometry(*scene, resources.view(), vertices, indices);
    ASSERT_TRUE(written.has_value());

    EXPECT_NEAR(vertices[0].positionX, 2.0F, 1.0e-5F);
    EXPECT_NEAR(vertices[0].positionY, -1.0F, 1.0e-5F);
    EXPECT_NEAR(vertices[2].positionX, -2.0F, 1.0e-5F);
    EXPECT_NEAR(vertices[2].positionY, 1.0F, 1.0e-5F);
}

TEST(BgfxSprite2DGeometryTest, FlipChangesUvWithoutChangingGeometry)
{
    FrameResourceScope resources;
    const FrameResourceRef texture = resources.texture(1);
    RenderSceneBuilder normalBuilder = makeBuilder();
    const std::array normalInput{sprite(texture, 1, 0.0F)};
    auto normalScene = commitScene(normalBuilder, normalInput);
    ASSERT_TRUE(normalScene.has_value());
    std::array<BgfxSprite2DVertex, 4> normalVertices{};
    std::array<u32, 6> normalIndices{};
    ASSERT_TRUE(writeSprite2DGeometry(*normalScene, resources.view(), normalVertices, normalIndices));

    RenderSceneBuilder flippedBuilder = makeBuilder();
    auto flipped = sprite(texture, 1, 0.0F);
    flipped.flipX = true;
    flipped.flipY = true;
    const std::array flippedInput{flipped};
    auto flippedScene = commitScene(flippedBuilder, flippedInput);
    ASSERT_TRUE(flippedScene.has_value());
    std::array<BgfxSprite2DVertex, 4> flippedVertices{};
    std::array<u32, 6> flippedIndices{};
    ASSERT_TRUE(writeSprite2DGeometry(*flippedScene, resources.view(), flippedVertices, flippedIndices));

    for (usize index = 0; index < normalVertices.size(); ++index)
    {
        EXPECT_FLOAT_EQ(flippedVertices[index].positionX, normalVertices[index].positionX);
        EXPECT_FLOAT_EQ(flippedVertices[index].positionY, normalVertices[index].positionY);
    }
    EXPECT_FLOAT_EQ(flippedVertices[0].textureU, 1.0F);
    EXPECT_FLOAT_EQ(flippedVertices[0].textureV, 0.0F);
    EXPECT_FLOAT_EQ(flippedVertices[2].textureU, 0.0F);
    EXPECT_FLOAT_EQ(flippedVertices[2].textureV, 1.0F);
}

TEST(BgfxSprite2DGeometryTest, MultipleTextureRefsPreserveRenderOrderAndCountContiguousBatches)
{
    FrameResourceScope resources;
    const FrameResourceRef atlasA = resources.texture(11);
    const FrameResourceRef atlasB = resources.texture(21);
    RenderSceneBuilder builder = makeBuilder();
    const std::array inputs{
        sprite(atlasA, 40, 4.0F, 2, 0),
        sprite(atlasA, 20, -1.0F, 0, 1),
        sprite(atlasB, 30, 2.0F, 1, 0),
        sprite(atlasA, 10, -3.0F, 0, 0),
    };
    auto scene = commitScene(builder, inputs);
    ASSERT_TRUE(scene.has_value()) << scene.error().message;
    ASSERT_EQ(scene->sprites2D().size(), 4U);
    EXPECT_EQ(scene->sprites2D()[0].stableEntityKey, 10U);
    EXPECT_EQ(scene->sprites2D()[1].stableEntityKey, 20U);
    EXPECT_EQ(scene->sprites2D()[2].stableEntityKey, 30U);
    EXPECT_EQ(scene->sprites2D()[3].stableEntityKey, 40U);
    EXPECT_EQ(scene->sprites2D()[0].texture, atlasA);
    EXPECT_EQ(scene->sprites2D()[1].texture, atlasA);
    EXPECT_EQ(scene->sprites2D()[2].texture, atlasB);
    EXPECT_EQ(scene->sprites2D()[3].texture, atlasA);

    auto requirements = checkedSprite2DFrame(*scene, resources.view());
    ASSERT_TRUE(requirements.has_value()) << requirements.error().message;
    EXPECT_EQ(requirements->spriteCount, 4U);
    EXPECT_EQ(requirements->vertexCount, 16U);
    EXPECT_EQ(requirements->indexCount, 24U);
    EXPECT_EQ(requirements->batchCount, 3U);

    std::array<BgfxSprite2DVertex, 16> vertices{};
    std::array<u32, 24> indices{};
    auto written = writeSprite2DGeometry(*scene, resources.view(), vertices, indices);
    ASSERT_TRUE(written.has_value()) << written.error().message;
    EXPECT_EQ(written->batchCount, 3U);

    constexpr u32 ExpectedAbgr = 0xC0014080U;
    expectVertex(vertices[0], -4.0F, -1.0F, 0.0F, 1.0F, ExpectedAbgr);
    expectVertex(vertices[4], -2.0F, -1.0F, 0.0F, 1.0F, ExpectedAbgr);
    expectVertex(vertices[8], 1.0F, -1.0F, 0.0F, 1.0F, ExpectedAbgr);
    expectVertex(vertices[12], 3.0F, -1.0F, 0.0F, 1.0F, ExpectedAbgr);

    constexpr std::array<u32, 24> ExpectedIndices{
        0,  1,  2,  0,  2,  3,  4,  5,  6,  4,  6,  7,
        8,  9,  10, 8,  10, 11, 12, 13, 14, 12, 14, 15,
    };
    EXPECT_TRUE(std::ranges::equal(indices, ExpectedIndices));
}

TEST(BgfxSprite2DGeometryTest, RejectsInvalidTextureRefInCorruptView)
{
    FrameResourceScope resources;
    const FrameResourceRef texture = resources.texture(1);
    RenderSceneBuilder builder = makeBuilder();
    const std::array inputs{sprite(texture, 1, 0.0F)};
    auto scene = commitScene(builder, inputs);
    ASSERT_TRUE(scene.has_value());

    const_cast<RenderSprite2DItem&>(scene->sprites2D()[0]).texture = {};
    auto requirements = checkedSprite2DFrame(*scene, resources.view());
    ASSERT_FALSE(requirements.has_value());
    EXPECT_EQ(requirements.error().code, RenderErrorCode::InvalidFrameResource);
}

TEST(BgfxSprite2DGeometryTest, CrossPacketTextureRefFailsBeforeAnyGeometryWrite)
{
    FrameResourceScope sceneResources;
    FrameResourceScope submittedResources;
    const FrameResourceRef texture = sceneResources.texture(1);
    static_cast<void>(submittedResources.texture(1));
    RenderSceneBuilder builder = makeBuilder();
    const std::array inputs{sprite(texture, 1, 0.0F)};
    auto scene = commitScene(builder, inputs);
    ASSERT_TRUE(scene.has_value());

    constexpr BgfxSprite2DVertex VertexSentinel{11.0F, 22.0F, 0.25F, 0.75F, 0x12345678U};
    constexpr u32 IndexSentinel = 0x87654321U;
    std::array<BgfxSprite2DVertex, 4> vertices;
    std::array<u32, 6> indices;
    vertices.fill(VertexSentinel);
    indices.fill(IndexSentinel);

    auto result = writeSprite2DGeometry(*scene, submittedResources.view(), vertices, indices);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, RenderErrorCode::InvalidFrameResource);
    EXPECT_TRUE(std::ranges::all_of(vertices, [&](const BgfxSprite2DVertex& vertex) {
        return vertex.positionX == VertexSentinel.positionX && vertex.positionY == VertexSentinel.positionY &&
               vertex.textureU == VertexSentinel.textureU && vertex.textureV == VertexSentinel.textureV &&
               vertex.abgr == VertexSentinel.abgr;
    }));
    EXPECT_TRUE(std::ranges::all_of(indices, [](u32 index) { return index == IndexSentinel; }));
}

TEST(BgfxSprite2DGeometryTest, RejectsTextureBindingKeyOutsideSpriteBindingRange)
{
    FrameResourceScope resources;
    const FrameResourceRef texture =
        resources.texture(static_cast<u64>((std::numeric_limits<u32>::max)()) + 1U);
    RenderSceneBuilder builder = makeBuilder();
    const std::array inputs{sprite(texture, 1, 0.0F)};
    auto scene = commitScene(builder, inputs);
    ASSERT_TRUE(scene.has_value());

    auto requirements = checkedSprite2DFrame(*scene, resources.view());
    ASSERT_FALSE(requirements.has_value());
    EXPECT_EQ(requirements.error().code, RenderErrorCode::InvalidFrameResource);
}

TEST(BgfxSprite2DGeometryTest, RejectsMissingCameraInCorruptView)
{
    FrameResourceScope resources;
    const FrameResourceRef texture = resources.texture(1);
    RenderSceneBuilder builder = makeBuilder();
    const std::array inputs{sprite(texture, 1, 0.0F)};
    auto scene = commitScene(builder, inputs);
    ASSERT_TRUE(scene.has_value());

    const_cast<std::optional<RenderCamera2D>&>(scene->camera2D()).reset();
    auto requirements = checkedSprite2DFrame(*scene, resources.view());
    ASSERT_FALSE(requirements.has_value());
    EXPECT_EQ(requirements.error().code, RenderErrorCode::InvalidRenderSceneInput);
}

TEST(BgfxSprite2DGeometryTest, InsufficientOutputCapacityLeavesBuffersUntouched)
{
    FrameResourceScope resources;
    const FrameResourceRef texture = resources.texture(1);
    RenderSceneBuilder builder = makeBuilder();
    const std::array inputs{sprite(texture, 1, 0.0F), sprite(texture, 2, 2.0F)};
    auto scene = commitScene(builder, inputs);
    ASSERT_TRUE(scene.has_value());

    constexpr BgfxSprite2DVertex VertexSentinel{11.0F, 22.0F, 0.25F, 0.75F, 0x12345678U};
    constexpr u32 IndexSentinel = 0x87654321U;
    std::array<BgfxSprite2DVertex, 8> vertices;
    std::array<u32, 12> indices;
    vertices.fill(VertexSentinel);
    indices.fill(IndexSentinel);

    auto shortVertices =
        writeSprite2DGeometry(*scene, resources.view(), std::span{vertices}.first(7), indices);
    ASSERT_FALSE(shortVertices.has_value());
    EXPECT_EQ(shortVertices.error().code, Core::CoreErrorCode::CapacityExceeded);
    EXPECT_TRUE(std::ranges::all_of(vertices, [&](const BgfxSprite2DVertex& vertex) {
        return vertex.positionX == VertexSentinel.positionX && vertex.positionY == VertexSentinel.positionY &&
               vertex.textureU == VertexSentinel.textureU && vertex.textureV == VertexSentinel.textureV &&
               vertex.abgr == VertexSentinel.abgr;
    }));
    EXPECT_TRUE(std::ranges::all_of(indices, [](u32 index) { return index == IndexSentinel; }));

    auto shortIndices =
        writeSprite2DGeometry(*scene, resources.view(), vertices, std::span{indices}.first(11));
    ASSERT_FALSE(shortIndices.has_value());
    EXPECT_EQ(shortIndices.error().code, Core::CoreErrorCode::CapacityExceeded);
    EXPECT_TRUE(std::ranges::all_of(vertices, [&](const BgfxSprite2DVertex& vertex) {
        return vertex.positionX == VertexSentinel.positionX && vertex.positionY == VertexSentinel.positionY &&
               vertex.textureU == VertexSentinel.textureU && vertex.textureV == VertexSentinel.textureV &&
               vertex.abgr == VertexSentinel.abgr;
    }));
    EXPECT_TRUE(std::ranges::all_of(indices, [](u32 index) { return index == IndexSentinel; }));
}

TEST(BgfxSprite2DGeometryTest, ReusesCallerOwnedStorageForThreeHundredWrites)
{
    FrameResourceScope resources;
    const FrameResourceRef texture = resources.texture(1);
    RenderSceneBuilder builder = makeBuilder();
    const std::array inputs{sprite(texture, 1, -1.0F), sprite(texture, 2, 1.0F)};
    auto scene = commitScene(builder, inputs);
    ASSERT_TRUE(scene.has_value());

    std::array<BgfxSprite2DVertex, 8> vertices{};
    std::array<u32, 12> indices{};
    BgfxSprite2DVertex* const fixedVertices = vertices.data();
    u32* const fixedIndices = indices.data();

    for (u32 iteration = 0; iteration < 300; ++iteration)
    {
        auto written = writeSprite2DGeometry(*scene, resources.view(), vertices, indices);
        ASSERT_TRUE(written.has_value());
        EXPECT_EQ(written->vertexCount, vertices.size());
        EXPECT_EQ(written->indexCount, indices.size());
        EXPECT_EQ(vertices.data(), fixedVertices);
        EXPECT_EQ(indices.data(), fixedIndices);
        EXPECT_EQ(indices[11], 7U);
    }
}

} // namespace
} // namespace Tina::Render::Bgfx
