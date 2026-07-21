#include <gtest/gtest.h>

#include <tina/core/error/Result.hpp>
#include <tina/render/RenderScene.hpp>
#include <tina/runtime/RuntimeErrors.hpp>

#include "../../../src/runtime/input/LastPresentedCamera2DLatch.hpp"

#include <memory>
#include <utility>

namespace Tina::Tests {
namespace {

using Runtime::Input::LastPresentedCamera2DLatch;

[[nodiscard]] Render::RenderCamera2DInput camera(float centerX, float centerY)
{
    return Render::RenderCamera2DInput{
        .stableCameraKey = 9,
        .centerX = centerX,
        .centerY = centerY,
        .worldWidth = 10.0F,
        .worldHeight = 10.0F,
        .actualPixelsPerMeter = 32.0F,
    };
}

[[nodiscard]] std::optional<Render::RenderSceneView> publishScene(Render::RenderSceneBuilder& builder,
                                                                  const Render::RenderCamera2DInput& cameraInput)
{
    if (!builder.beginFrame())
    {
        return std::nullopt;
    }
    if (!builder.writer().setCamera2D(cameraInput))
    {
        return std::nullopt;
    }
    auto scene = builder.commit();
    if (!scene)
    {
        return std::nullopt;
    }
    return *scene;
}

[[nodiscard]] std::optional<Render::RenderSceneView> publishEmptyScene(Render::RenderSceneBuilder& builder)
{
    if (!builder.beginFrame())
    {
        return std::nullopt;
    }
    auto scene = builder.commit();
    if (!scene)
    {
        return std::nullopt;
    }
    return *scene;
}

TEST(LastPresentedCamera2DLatchTest, PickFailsUntilPresentedThenMapsCenter)
{
    LastPresentedCamera2DLatch latch;
    EXPECT_FALSE(latch.hasCamera());

    auto before = latch.pickLogical(50.0, 50.0, 100, 100, 7);
    ASSERT_FALSE(before.has_value());
    EXPECT_EQ(before.error().code, RuntimeErrorCode::LifecycleInvariantViolation);

    auto builder = Render::RenderSceneBuilder::Create(Render::RenderSceneCapacity{});
    ASSERT_TRUE(builder.has_value()) << (builder ? "" : builder.error().message);
    auto scene = publishScene(*builder, camera(0.0F, 0.0F));
    ASSERT_TRUE(scene.has_value());

    latch.notePresented(*scene, /*surfaceRevision=*/42);
    ASSERT_TRUE(latch.hasCamera());
    EXPECT_EQ(latch.surfaceRevision(), 42U);
    EXPECT_GE(latch.cameraRevision(), 1U);

    auto sample = latch.pickLogical(50.0, 50.0, 100, 100, 7);
    ASSERT_TRUE(sample.has_value()) << (sample ? "" : sample.error().message);
    EXPECT_TRUE(sample->hit);
    EXPECT_FLOAT_EQ(sample->worldX, 0.0F);
    EXPECT_FLOAT_EQ(sample->worldY, 0.0F);
    EXPECT_EQ(sample->surfaceRevision, 42U);
    EXPECT_EQ(sample->cameraRevision, latch.cameraRevision());
    EXPECT_EQ(sample->inputSequence, 7U);
    EXPECT_EQ(sample->stableCameraKey, 9U);
}

TEST(LastPresentedCamera2DLatchTest, PresentWithoutCameraClearsLatch)
{
    LastPresentedCamera2DLatch latch;
    auto builder = Render::RenderSceneBuilder::Create(Render::RenderSceneCapacity{});
    ASSERT_TRUE(builder.has_value()) << (builder ? "" : builder.error().message);
    auto withCamera = publishScene(*builder, camera(1.0F, 2.0F));
    ASSERT_TRUE(withCamera.has_value());
    latch.notePresented(*withCamera, 1);
    ASSERT_TRUE(latch.hasCamera());

    auto emptyBuilder = Render::RenderSceneBuilder::Create(Render::RenderSceneCapacity{});
    ASSERT_TRUE(emptyBuilder.has_value()) << (emptyBuilder ? "" : emptyBuilder.error().message);
    auto emptyScene = publishEmptyScene(*emptyBuilder);
    ASSERT_TRUE(emptyScene.has_value());
    latch.notePresented(*emptyScene, 2);
    EXPECT_FALSE(latch.hasCamera());

    auto failed = latch.pickLogical(50.0, 50.0, 100, 100, 1);
    ASSERT_FALSE(failed.has_value());
    EXPECT_EQ(failed.error().code, RuntimeErrorCode::LifecycleInvariantViolation);
}

TEST(LastPresentedCamera2DLatchTest, LockedSampleSurvivesLaterCameraChange)
{
    LastPresentedCamera2DLatch latch;
    auto firstBuilder = Render::RenderSceneBuilder::Create(Render::RenderSceneCapacity{});
    ASSERT_TRUE(firstBuilder.has_value());
    auto first = publishScene(*firstBuilder, camera(0.0F, 0.0F));
    ASSERT_TRUE(first.has_value());
    latch.notePresented(*first, 10);
    const u64 lockedRevision = latch.cameraRevision();

    auto locked = latch.pickLogical(25.0, 75.0, 100, 100, 99);
    ASSERT_TRUE(locked.has_value()) << (locked ? "" : locked.error().message);
    ASSERT_TRUE(locked->hit);
    EXPECT_EQ(locked->cameraRevision, lockedRevision);

    auto secondBuilder = Render::RenderSceneBuilder::Create(Render::RenderSceneCapacity{});
    ASSERT_TRUE(secondBuilder.has_value());
    auto second = publishScene(*secondBuilder, camera(100.0F, 100.0F));
    ASSERT_TRUE(second.has_value());
    // Extraction can move the live camera, but the latch still holds the last
    // presented snapshot until the next present updates it.
    auto stillLocked = latch.pickLogical(25.0, 75.0, 100, 100, 100);
    ASSERT_TRUE(stillLocked.has_value()) << (stillLocked ? "" : stillLocked.error().message);
    EXPECT_FLOAT_EQ(stillLocked->worldX, locked->worldX);
    EXPECT_FLOAT_EQ(stillLocked->worldY, locked->worldY);
    EXPECT_EQ(stillLocked->cameraRevision, lockedRevision);

    latch.notePresented(*second, 11);
    auto updated = latch.pickLogical(25.0, 75.0, 100, 100, 101);
    ASSERT_TRUE(updated.has_value()) << (updated ? "" : updated.error().message);
    EXPECT_NE(updated->worldX, locked->worldX);
    EXPECT_NE(updated->worldY, locked->worldY);
    EXPECT_GT(latch.cameraRevision(), lockedRevision);
}

TEST(LastPresentedCamera2DLatchTest, ViewportMissIsExplicitNoHit)
{
    LastPresentedCamera2DLatch latch;
    auto builder = Render::RenderSceneBuilder::Create(Render::RenderSceneCapacity{});
    ASSERT_TRUE(builder.has_value());
    Render::RenderCamera2DInput cam = camera(0.0F, 0.0F);
    cam.normalizedViewport = {.x = 0.25F, .y = 0.25F, .width = 0.5F, .height = 0.5F};
    auto scene = publishScene(*builder, cam);
    ASSERT_TRUE(scene.has_value());
    latch.notePresented(*scene, 3);

    auto miss = latch.pickLogical(5.0, 5.0, 100, 100, 1);
    ASSERT_TRUE(miss.has_value()) << (miss ? "" : miss.error().message);
    EXPECT_FALSE(miss->hit);
    EXPECT_EQ(miss->surfaceRevision, 3U);
}

} // namespace
} // namespace Tina::Tests
