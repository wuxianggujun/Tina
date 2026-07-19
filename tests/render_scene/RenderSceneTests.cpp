#include <tina/render/RenderErrors.hpp>
#include <tina/render/RenderScene.hpp>

#include <gtest/gtest.h>

#include <limits>
#include <memory_resource>
#include <new>
#include <numbers>

namespace Tina::Render {
namespace {

class CountingResource final : public std::pmr::memory_resource {
  public:
    usize allocations = 0;
    usize deallocations = 0;
    bool rejectAllocations = false;

  private:
    void* do_allocate(usize bytes, usize alignment) override
    {
        if (rejectAllocations)
        {
            throw std::bad_alloc{};
        }
        ++allocations;
        return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    }

    void do_deallocate(void* pointer, usize bytes, usize alignment) override
    {
        ++deallocations;
        std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
    }

    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }
};

[[nodiscard]] RenderSceneBuilder makeBuilder(u32 capacity = 16)
{
    auto result = RenderSceneBuilder::Create(RenderSceneCapacity{capacity});
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return std::move(*result);
}

[[nodiscard]] RenderCamera2DInput camera(float centerX = 0.0F, float centerY = 0.0F)
{
    return RenderCamera2DInput{
        .stableCameraKey = 7,
        .centerX = centerX,
        .centerY = centerY,
        .rotationRadians = 0.0F,
        .worldWidth = 10.0F,
        .worldHeight = 10.0F,
        .actualPixelsPerMeter = 10.0F,
    };
}

[[nodiscard]] RenderSprite2DInput sprite(u32 key, u64 stableKey, float x, float y,
                                         i16 layer = 0, i32 order = 0)
{
    return RenderSprite2DInput{
        .spriteKey = key,
        .stableEntityKey = stableKey,
        .centerX = x,
        .centerY = y,
        .sortingLayer = layer,
        .orderInLayer = order,
    };
}

TEST(RenderSceneBuilderTest, RejectsInvalidCapacityBeforeAllocating)
{
    CountingResource resource;
    const auto result = RenderSceneBuilder::Create(RenderSceneCapacity{0}, resource);
    EXPECT_FALSE(result);
    EXPECT_EQ(result.error().code, RenderErrorCode::InvalidRenderSceneCapacity);
    EXPECT_EQ(resource.allocations, 0U);
}

TEST(RenderSceneBuilderTest, MapsFixedStorageAllocationFailure)
{
    CountingResource resource;
    resource.rejectAllocations = true;
    const auto result = RenderSceneBuilder::Create(RenderSceneCapacity{4}, resource);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, RenderErrorCode::RenderSceneStorageAllocationFailed);
}

TEST(RenderSceneBuilderTest, SortsCullsAndSnapsWithoutChangingInput)
{
    RenderSceneBuilder builder = makeBuilder();
    ASSERT_TRUE(builder.beginFrame());
    RenderCamera2DInput cameraInput = camera(0.13F, -0.07F);
    cameraInput.pixelSnap = RenderPixelSnapPolicy::CameraAndSprites;
    ASSERT_TRUE(builder.writer().setCamera2D(cameraInput));

    RenderSprite2DInput farAway = sprite(99, 99, 100.0F, 100.0F);
    ASSERT_TRUE(builder.writer().addSprite2D(farAway));
    ASSERT_TRUE(builder.writer().addSprite2D(sprite(2, 20, 0.14F, 0.04F, 2, 0)));
    ASSERT_TRUE(builder.writer().addSprite2D(sprite(1, 10, 0.12F, 0.02F, 1, 4)));

    auto committed = builder.commit();
    ASSERT_TRUE(committed.has_value()) << committed.error().message;
    ASSERT_TRUE(committed->camera2D().has_value());
    EXPECT_FLOAT_EQ(committed->camera2D()->centerX, 0.1F);
    ASSERT_EQ(committed->sprites2D().size(), 2U);
    EXPECT_EQ(committed->sprites2D()[0].spriteKey, 1U);
    EXPECT_EQ(committed->sprites2D()[1].spriteKey, 2U);
    EXPECT_EQ(committed->statistics().culledSpriteCount, 1U);
    EXPECT_EQ(committed->statistics().visibleSpriteCount, 2U);
    EXPECT_FLOAT_EQ(committed->sprites2D()[0].centerX, 0.1F);
}

TEST(RenderSceneBuilderTest, ConservativelyCullsRotatedSpritesAtTheCameraBoundary)
{
    RenderSceneBuilder builder = makeBuilder();
    ASSERT_TRUE(builder.beginFrame());
    RenderCamera2DInput cameraInput = camera();
    cameraInput.worldWidth = 4.0F;
    cameraInput.worldHeight = 4.0F;
    ASSERT_TRUE(builder.writer().setCamera2D(cameraInput));

    RenderSprite2DInput intersectsAfterRotation = sprite(1, 1, 3.05F, 0.0F);
    intersectsAfterRotation.widthMeters = 2.0F;
    intersectsAfterRotation.heightMeters = 1.0F;
    intersectsAfterRotation.rotationRadians = std::numbers::pi_v<float> * 0.25F;
    ASSERT_TRUE(builder.writer().addSprite2D(intersectsAfterRotation));

    RenderSprite2DInput outsideAfterRotation = intersectsAfterRotation;
    outsideAfterRotation.spriteKey = 2;
    outsideAfterRotation.stableEntityKey = 2;
    outsideAfterRotation.centerX = 3.2F;
    ASSERT_TRUE(builder.writer().addSprite2D(outsideAfterRotation));

    auto committed = builder.commit();
    ASSERT_TRUE(committed.has_value()) << committed.error().message;
    ASSERT_EQ(committed->sprites2D().size(), 1U);
    EXPECT_EQ(committed->sprites2D().front().spriteKey, 1U);
    EXPECT_EQ(committed->statistics().culledSpriteCount, 1U);
}

TEST(RenderSceneBuilderTest, UsesEntityThenInsertionOrderForEqualLayers)
{
    RenderSceneBuilder builder = makeBuilder();
    ASSERT_TRUE(builder.beginFrame());
    ASSERT_TRUE(builder.writer().setCamera2D(camera()));
    ASSERT_TRUE(builder.writer().addSprite2D(sprite(3, 30, 0.0F, 0.0F, 2, 7)));
    ASSERT_TRUE(builder.writer().addSprite2D(sprite(2, 20, 0.0F, 0.0F, 2, 7)));
    ASSERT_TRUE(builder.writer().addSprite2D(sprite(1, 20, 0.0F, 0.0F, 2, 7)));

    auto committed = builder.commit();
    ASSERT_TRUE(committed.has_value()) << committed.error().message;
    ASSERT_EQ(committed->sprites2D().size(), 3U);
    EXPECT_EQ(committed->sprites2D()[0].spriteKey, 2U);
    EXPECT_EQ(committed->sprites2D()[1].spriteKey, 1U);
    EXPECT_EQ(committed->sprites2D()[2].spriteKey, 3U);
    EXPECT_LT(committed->sprites2D()[0].insertionOrder, committed->sprites2D()[1].insertionOrder);
}

TEST(RenderSceneBuilderTest, RequiresOneCameraForWorldSpritesAndRejectsDuplicates)
{
    RenderSceneBuilder builder = makeBuilder();
    ASSERT_TRUE(builder.beginFrame());
    ASSERT_TRUE(builder.writer().addSprite2D(sprite(1, 1, 0.0F, 0.0F)));
    auto missingCamera = builder.commit();
    ASSERT_FALSE(missingCamera);
    EXPECT_EQ(missingCamera.error().code, RenderErrorCode::RenderSceneMissingCamera);

    ASSERT_TRUE(builder.beginFrame());
    ASSERT_TRUE(builder.writer().setCamera2D(camera()));
    const auto duplicate = builder.writer().setCamera2D(camera());
    ASSERT_FALSE(duplicate);
    EXPECT_EQ(duplicate.error().code, RenderErrorCode::RenderSceneCameraConflict);
    builder.rollback();
}

TEST(RenderSceneBuilderTest, PrunesInvisibleAndTransparentSpritesAndReportsCapacity)
{
    RenderSceneBuilder builder = makeBuilder(1);
    ASSERT_TRUE(builder.beginFrame());
    ASSERT_TRUE(builder.writer().setCamera2D(camera()));

    auto invisible = sprite(1, 1, 0.0F, 0.0F);
    invisible.visible = false;
    ASSERT_TRUE(builder.writer().addSprite2D(invisible));
    auto transparent = sprite(2, 2, 0.0F, 0.0F);
    transparent.alpha = 0;
    ASSERT_TRUE(builder.writer().addSprite2D(transparent));
    ASSERT_TRUE(builder.writer().addSprite2D(sprite(3, 3, 0.0F, 0.0F)));
    const auto overflow = builder.writer().addSprite2D(sprite(4, 4, 0.0F, 0.0F));
    ASSERT_FALSE(overflow);
    EXPECT_EQ(overflow.error().code, RenderErrorCode::RenderSceneCapacityExceeded);
    const auto commit = builder.commit();
    ASSERT_FALSE(commit);
    EXPECT_EQ(commit.error().code, RenderErrorCode::RenderSceneCapacityExceeded);
}

TEST(RenderSceneBuilderTest, RejectsInvalidIdentityAndDerivedGeometryAtomically)
{
    RenderSceneBuilder builder = makeBuilder();
    ASSERT_TRUE(builder.beginFrame());

    RenderCamera2DInput invalidCamera = camera();
    invalidCamera.stableCameraKey = 0;
    const auto cameraFailure = builder.writer().setCamera2D(invalidCamera);
    ASSERT_FALSE(cameraFailure);
    EXPECT_EQ(cameraFailure.error().code, RenderErrorCode::InvalidRenderSceneInput);

    const auto stickyFailure = builder.writer().setCamera2D(camera());
    ASSERT_FALSE(stickyFailure);
    EXPECT_EQ(stickyFailure.error().code, RenderErrorCode::InvalidRenderSceneInput);
    const auto failedCommit = builder.commit();
    ASSERT_FALSE(failedCommit);
    EXPECT_EQ(failedCommit.error().code, RenderErrorCode::InvalidRenderSceneInput);
    EXPECT_TRUE(builder.publishedView().empty());

    ASSERT_TRUE(builder.beginFrame());
    ASSERT_TRUE(builder.writer().setCamera2D(camera()));
    RenderSprite2DInput overflow = sprite(1, 1, 0.0F, 0.0F);
    overflow.widthMeters = (std::numeric_limits<float>::max)();
    overflow.scaleX = 2.0F;
    const auto spriteFailure = builder.writer().addSprite2D(overflow);
    ASSERT_FALSE(spriteFailure);
    EXPECT_EQ(spriteFailure.error().code, RenderErrorCode::InvalidRenderSceneInput);
    const auto secondFailedCommit = builder.commit();
    ASSERT_FALSE(secondFailedCommit);
    EXPECT_EQ(secondFailedCommit.error().code, RenderErrorCode::InvalidRenderSceneInput);

    const RenderSceneBuilderStatistics statistics = builder.statistics();
    EXPECT_EQ(statistics.invalidInputFailureCount, 2U);
    EXPECT_EQ(statistics.rolledBackBuildCount, 2U);
}

TEST(RenderSceneBuilderTest, ReplacementBuildInvalidatesOldPublicationAndFailurePublishesNothing)
{
    RenderSceneBuilder builder = makeBuilder(1);
    ASSERT_TRUE(builder.beginFrame());
    ASSERT_TRUE(builder.writer().setCamera2D(camera()));
    ASSERT_TRUE(builder.writer().addSprite2D(sprite(1, 1, 0.0F, 0.0F)));
    auto first = builder.commit();
    ASSERT_TRUE(first.has_value());
    ASSERT_EQ(first->sprites2D().size(), 1U);

    ASSERT_TRUE(builder.beginFrame());
    EXPECT_TRUE(builder.publishedView().empty());
    ASSERT_TRUE(builder.writer().setCamera2D(camera()));
    ASSERT_TRUE(builder.writer().addSprite2D(sprite(2, 2, 0.0F, 0.0F)));
    ASSERT_FALSE(builder.writer().addSprite2D(sprite(3, 3, 0.0F, 0.0F)));
    ASSERT_FALSE(builder.commit());
    EXPECT_TRUE(builder.publishedView().empty());
}

TEST(RenderSceneBuilderTest, ReusesFixedStorageAcrossThreeHundredFrames)
{
    CountingResource resource;
    {
        auto builderResult = RenderSceneBuilder::Create(RenderSceneCapacity{4}, resource);
        ASSERT_TRUE(builderResult.has_value());
        RenderSceneBuilder builder = std::move(*builderResult);
        const usize allocationsAfterCreate = resource.allocations;

        for (u32 frame = 0; frame < 300; ++frame)
        {
            ASSERT_TRUE(builder.beginFrame());
            ASSERT_TRUE(builder.writer().setCamera2D(camera()));
            ASSERT_TRUE(builder.writer().addSprite2D(sprite(1, frame + 1U, 0.0F, 0.0F)));
            auto committed = builder.commit();
            ASSERT_TRUE(committed.has_value());
            ASSERT_EQ(committed->sprites2D().size(), 1U);
        }

        EXPECT_EQ(resource.allocations, allocationsAfterCreate);
    }
    EXPECT_EQ(resource.allocations, resource.deallocations);
}

TEST(RenderSceneBuilderTest, MoveTransfersFixedStorageExactlyOnce)
{
    CountingResource resource;
    {
        auto builderResult = RenderSceneBuilder::Create(RenderSceneCapacity{4}, resource);
        ASSERT_TRUE(builderResult.has_value());
        const usize allocationsAfterCreate = resource.allocations;
        RenderSceneBuilder original = std::move(*builderResult);
        RenderSceneBuilder moved = std::move(original);
        EXPECT_EQ(resource.allocations, allocationsAfterCreate);

        ASSERT_TRUE(moved.beginFrame());
        ASSERT_TRUE(moved.writer().setCamera2D(camera()));
        ASSERT_TRUE(moved.writer().addSprite2D(sprite(1, 1, 0.0F, 0.0F)));
        ASSERT_TRUE(moved.commit());
        EXPECT_EQ(resource.allocations, allocationsAfterCreate);
    }
    EXPECT_EQ(resource.allocations, resource.deallocations);
}

} // namespace
} // namespace Tina::Render
