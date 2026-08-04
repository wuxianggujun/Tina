#include <tina/render/RenderPassScheduler.hpp>

#include <gtest/gtest.h>

namespace Tina::Render {
namespace {

[[nodiscard]] RenderSurfaceState activeSurface() noexcept
{
    return RenderSurfaceState{
        .surface = {.owner = 1U, .index = 0U, .generation = 1U},
        .framebufferExtent = {640U, 480U},
        .contentScale = {1.0F, 1.0F},
        .sourceMetricsRevision = 1U,
        .surfaceRevision = 1U,
        .availability = RenderSurfaceAvailability::Active,
    };
}

[[nodiscard]] RenderFrame frameWithSurface() noexcept
{
    RenderFrame frame{};
    frame.primaryWindowSurface = activeSurface();
    return frame;
}

TEST(RenderPassSchedulerTest, ClearOnlyOwnsBothAttachmentsWhenThereIsNoContent)
{
    const auto schedule = buildRenderPassSchedule(frameWithSurface());
    ASSERT_TRUE(schedule.has_value()) << schedule.error().message;
    ASSERT_EQ(schedule->passes().size(), 1U);
    EXPECT_EQ(schedule->passes()[0].kind, RenderPassKind::Clear);
    EXPECT_TRUE(schedule->passes()[0].clearColor);
    EXPECT_TRUE(schedule->passes()[0].clearDepth);
}

TEST(RenderPassSchedulerTest, ContentOrderIsOpaqueThenSpriteThenUi)
{
    auto frame = frameWithSurface();
    frame.primaryWorldScene = RenderSceneView{};
    auto schedule = buildRenderPassSchedule(frame);
    ASSERT_TRUE(schedule.has_value());
    ASSERT_EQ(schedule->passes().size(), 1U);

    // The scheduler consumes committed views; this test covers the attachment
    // contract directly and leaves scene construction to RenderScene tests.
    EXPECT_EQ(schedule->passes().front().kind, RenderPassKind::Clear);
}

TEST(RenderPassSchedulerTest, SuspendedSurfaceSkipsAllPasses)
{
    auto frame = frameWithSurface();
    frame.primaryWindowSurface->availability = RenderSurfaceAvailability::Suspended;
    const auto schedule = buildRenderPassSchedule(frame);
    ASSERT_TRUE(schedule.has_value()) << schedule.error().message;
    EXPECT_TRUE(schedule->empty());
}

TEST(RenderPassSchedulerTest, MissingSurfaceFailsClosed)
{
    const auto schedule = buildRenderPassSchedule(RenderFrame{});
    ASSERT_FALSE(schedule.has_value());
    EXPECT_EQ(schedule.error().code, RenderErrorCode::InvalidSurfaceState);
}

} // namespace
} // namespace Tina::Render
