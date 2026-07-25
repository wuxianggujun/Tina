#include "BgfxRetirementTimeline.hpp"

#include <gtest/gtest.h>

#include <limits>

namespace Tina::Render::Bgfx::RetirementDetail {
namespace {

TEST(BgfxRetirementPolicyTests, SelectsMarkerImmediateDestroyOrAtomicRejection)
{
    EXPECT_EQ(selectRetirementDisposition(true, false),
              RetirementDisposition::QueueMarker);
    EXPECT_EQ(selectRetirementDisposition(true, true),
              RetirementDisposition::QueueMarker);
    EXPECT_EQ(selectRetirementDisposition(false, false),
              RetirementDisposition::DestroyImmediately);
    EXPECT_EQ(selectRetirementDisposition(false, true),
              RetirementDisposition::RejectExternalPin);
}

TEST(BgfxRetirementTimelineTests, MarkerCompletesOnlyAtReadbackReadyFrame)
{
    BgfxRetirementTimeline timeline;
    timeline.queue(2);

    ASSERT_TRUE(timeline.needsMarker());
    ASSERT_TRUE(timeline.beginMarker(12));
    EXPECT_EQ(timeline.queuedCount(), 0U);
    EXPECT_EQ(timeline.waitingCount(), 2U);
    EXPECT_EQ(timeline.completeThrough(11), 0U);
    EXPECT_EQ(timeline.completeThrough(12), 2U);
    EXPECT_EQ(timeline.pendingCount(), 0U);
    EXPECT_FALSE(timeline.markerInFlight());
}

TEST(BgfxRetirementTimelineTests, LaterRequestsWaitForNextMarker)
{
    BgfxRetirementTimeline timeline;
    timeline.queue();
    ASSERT_TRUE(timeline.beginMarker(7));

    timeline.queue(3);
    EXPECT_FALSE(timeline.needsMarker());
    EXPECT_EQ(timeline.completeThrough(7), 1U);
    EXPECT_TRUE(timeline.needsMarker());
    ASSERT_TRUE(timeline.beginMarker(10));
    EXPECT_EQ(timeline.completeThrough(10), 3U);
}

TEST(BgfxRetirementTimelineTests, ReadyFrameZeroIsAValidMarker)
{
    BgfxRetirementTimeline timeline;
    timeline.queue();
    ASSERT_TRUE(timeline.beginMarker(0));
    EXPECT_TRUE(timeline.markerInFlight());
    EXPECT_EQ(timeline.completeThrough(0), 1U);
}

TEST(BgfxRetirementTimelineTests, FrameComparisonHandlesUint32Wrap)
{
    constexpr Core::u32 Maximum = (std::numeric_limits<Core::u32>::max)();
    EXPECT_FALSE(BgfxRetirementTimeline::frameReached(Maximum - 1U, 1U));
    EXPECT_TRUE(BgfxRetirementTimeline::frameReached(1U, 1U));
    EXPECT_TRUE(BgfxRetirementTimeline::frameReached(2U, 1U));

    BgfxRetirementTimeline timeline;
    timeline.queue(4);
    ASSERT_TRUE(timeline.beginMarker(1U));
    EXPECT_EQ(timeline.completeThrough(Maximum), 0U);
    EXPECT_EQ(timeline.completeThrough(1U), 4U);
}

TEST(BgfxRetirementTimelineTests, EmptyOrActiveTimelineRejectsMarkerSubmission)
{
    BgfxRetirementTimeline timeline;
    EXPECT_FALSE(timeline.beginMarker(1));
    EXPECT_EQ(timeline.completeThrough(1), 0U);

    timeline.queue();
    ASSERT_TRUE(timeline.beginMarker(2));
    timeline.queue();
    EXPECT_FALSE(timeline.beginMarker(3));
}

} // namespace
} // namespace Tina::Render::Bgfx::RetirementDetail
