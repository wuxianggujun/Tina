#include "BgfxSurfaceFramePlanner.hpp"

#include <tina/render/RenderErrors.hpp>

#include <gtest/gtest.h>

namespace Tina::Render::Bgfx {
namespace {

[[nodiscard]] constexpr RenderSurfaceState
activeSurface(RenderSurfaceExtent extent = {640U, 480U}) noexcept
{
    return RenderSurfaceState{
        .surface = {.owner = 1U, .index = 0U, .generation = 1U},
        .framebufferExtent = extent,
        .contentScale = {1.0F, 1.0F},
        .sourceMetricsRevision = 1U,
        .surfaceRevision = 1U,
        .availability = RenderSurfaceAvailability::Active,
    };
}

[[nodiscard]] constexpr RenderSurfaceState suspendedSurface() noexcept
{
    auto surface = activeSurface({0U, 0U});
    surface.availability = RenderSurfaceAvailability::Suspended;
    return surface;
}

void expectExtent(RenderSurfaceExtent actual, u32 expectedWidth, u32 expectedHeight)
{
    EXPECT_EQ(actual.width, expectedWidth);
    EXPECT_EQ(actual.height, expectedHeight);
}

TEST(BgfxSurfaceFramePlannerTest, InitialBootstrapUsesActiveExtentOrOneByOneFallback)
{
    const auto activeBootstrap =
        BgfxSurfaceFramePlanner::bootstrapBackbufferExtent(activeSurface({1280U, 720U}));
    expectExtent(activeBootstrap, 1280U, 720U);

    const auto suspendedBootstrap = BgfxSurfaceFramePlanner::bootstrapBackbufferExtent(suspendedSurface());
    expectExtent(suspendedBootstrap, 1U, 1U);
}

TEST(BgfxSurfaceFramePlannerTest, ActiveResizePlansSubmitResetToCurrentExtent)
{
    const auto previous = activeSurface({640U, 480U});
    auto current = activeSurface({800U, 600U});
    current.sourceMetricsRevision = 2U;
    current.surfaceRevision = 2U;

    auto plan = BgfxSurfaceFramePlanner::planFrame(previous, current, previous.framebufferExtent);

    ASSERT_TRUE(plan.has_value()) << plan.error().message;
    EXPECT_EQ(plan->kind, BgfxSurfaceFramePlanKind::Submit);
    EXPECT_TRUE(plan->shouldSubmit());
    EXPECT_TRUE(plan->resetBackbuffer);
    expectExtent(plan->targetExtent, 800U, 600U);
}

TEST(BgfxSurfaceFramePlannerTest, ActiveResumeAlwaysPlansReset)
{
    const auto previous = suspendedSurface();
    auto current = activeSurface({640U, 480U});
    current.sourceMetricsRevision = 2U;
    current.surfaceRevision = 2U;
    const RenderSurfaceExtent appliedBackbuffer = current.framebufferExtent;

    auto plan = BgfxSurfaceFramePlanner::planFrame(previous, current, appliedBackbuffer);

    ASSERT_TRUE(plan.has_value()) << plan.error().message;
    EXPECT_EQ(plan->kind, BgfxSurfaceFramePlanKind::Submit);
    EXPECT_TRUE(plan->resetBackbuffer);
    expectExtent(plan->targetExtent, 640U, 480U);
}

TEST(BgfxSurfaceFramePlannerTest, ContentScaleOnlyChangeSubmitsWithoutReset)
{
    const auto previous = activeSurface({640U, 480U});
    auto current = previous;
    current.contentScale = {2.0F, 2.0F};
    current.sourceMetricsRevision = 2U;
    current.surfaceRevision = 2U;

    auto plan = BgfxSurfaceFramePlanner::planFrame(previous, current, previous.framebufferExtent);

    ASSERT_TRUE(plan.has_value()) << plan.error().message;
    EXPECT_EQ(plan->kind, BgfxSurfaceFramePlanKind::Submit);
    EXPECT_FALSE(plan->resetBackbuffer);
    expectExtent(plan->targetExtent, 640U, 480U);
}

TEST(BgfxSurfaceFramePlannerTest, SuspendedSurfaceSkipsAndKeepsAppliedExtent)
{
    const auto previous = activeSurface({640U, 480U});
    auto current = suspendedSurface();
    current.sourceMetricsRevision = 2U;
    current.surfaceRevision = 2U;
    constexpr RenderSurfaceExtent appliedBackbuffer{640U, 480U};

    auto plan = BgfxSurfaceFramePlanner::planFrame(previous, current, appliedBackbuffer);

    ASSERT_TRUE(plan.has_value()) << plan.error().message;
    EXPECT_EQ(plan->kind, BgfxSurfaceFramePlanKind::Skip);
    EXPECT_FALSE(plan->shouldSubmit());
    EXPECT_FALSE(plan->resetBackbuffer);
    expectExtent(plan->targetExtent, 640U, 480U);
}

TEST(BgfxSurfaceFramePlannerTest, ValidatesActiveViewExtentAgainstU16Limits)
{
    constexpr u32 maxViewExtent = BgfxSurfaceFramePlanner::MaxViewRectExtent;

    auto validMax = activeSurface({maxViewExtent, maxViewExtent});
    EXPECT_TRUE(BgfxSurfaceFramePlanner::validateViewExtent(validMax).has_value());

    auto tooWide = activeSurface({maxViewExtent + 1U, maxViewExtent});
    auto wideResult = BgfxSurfaceFramePlanner::validateViewExtent(tooWide);
    ASSERT_FALSE(wideResult.has_value());
    EXPECT_EQ(wideResult.error().code, RenderErrorCode::SurfaceReconfigureFailed);

    auto widePlan = BgfxSurfaceFramePlanner::planFrame(validMax, tooWide, validMax.framebufferExtent);
    ASSERT_FALSE(widePlan.has_value());
    EXPECT_EQ(widePlan.error().code, RenderErrorCode::SurfaceReconfigureFailed);

    auto tooTall = activeSurface({maxViewExtent, maxViewExtent + 1U});
    auto tallResult = BgfxSurfaceFramePlanner::validateViewExtent(tooTall);
    ASSERT_FALSE(tallResult.has_value());
    EXPECT_EQ(tallResult.error().code, RenderErrorCode::SurfaceReconfigureFailed);

    auto suspended = suspendedSurface();
    suspended.framebufferExtent = {maxViewExtent + 1U, maxViewExtent + 1U};
    EXPECT_TRUE(BgfxSurfaceFramePlanner::validateViewExtent(suspended).has_value());
}

TEST(BgfxSurfaceFramePlannerTest, IgnoresRevisionOrderingHandledBySurfaceStateTracker)
{
    auto previous = activeSurface({640U, 480U});
    previous.sourceMetricsRevision = 10U;
    previous.surfaceRevision = 10U;

    auto current = previous;
    current.sourceMetricsRevision = 2U;
    current.surfaceRevision = 2U;

    auto plan = BgfxSurfaceFramePlanner::planFrame(previous, current, previous.framebufferExtent);

    ASSERT_TRUE(plan.has_value()) << plan.error().message;
    EXPECT_EQ(plan->kind, BgfxSurfaceFramePlanKind::Submit);
    EXPECT_FALSE(plan->resetBackbuffer);
    expectExtent(plan->targetExtent, 640U, 480U);
}

} // namespace
} // namespace Tina::Render::Bgfx
