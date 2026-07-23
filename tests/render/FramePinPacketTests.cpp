#include <gtest/gtest.h>

#include <tina/render/FramePin.hpp>
#include <tina/render/RenderFramePacket.hpp>
#include <tina/render/RenderErrors.hpp>

#include <atomic>

namespace Tina::Tests {
namespace {

std::atomic<int> g_releaseCount{0};

void countingRelease(void* userData) noexcept
{
    ++g_releaseCount;
    delete static_cast<int*>(userData);
}

TEST(FramePinTest, ReleaseRunsExactlyOnce)
{
    g_releaseCount.store(0);
    {
        Render::FramePin pin{
            Render::FramePinKind::Custom,
            7,
            new int{42},
            &countingRelease,
        };
        EXPECT_TRUE(pin.hasValue());
        pin.release();
        EXPECT_FALSE(pin.hasValue());
        EXPECT_EQ(g_releaseCount.load(), 1);
        pin.release();
        EXPECT_EQ(g_releaseCount.load(), 1);
    }
    EXPECT_EQ(g_releaseCount.load(), 1);
}

TEST(FramePinTest, MoveTransfersOwnershipWithoutDoubleRelease)
{
    g_releaseCount.store(0);
    Render::FramePin a{
        Render::FramePinKind::AssetLease,
        1,
        new int{1},
        &countingRelease,
    };
    Render::FramePin b{std::move(a)};
    EXPECT_FALSE(a.hasValue());
    EXPECT_TRUE(b.hasValue());
    b.release();
    EXPECT_EQ(g_releaseCount.load(), 1);
}

TEST(NullSubmissionCompletionLedgerTest, BeginCompleteBalancesInflight)
{
    Render::NullSubmissionCompletionLedger ledger{4};
    auto t1 = ledger.beginSubmitted(1);
    ASSERT_TRUE(t1.has_value());
    auto t2 = ledger.beginSubmitted(2);
    ASSERT_TRUE(t2.has_value());
    EXPECT_EQ(ledger.inflightCount(), 2U);

    ASSERT_TRUE(ledger.complete(*t1).has_value());
    EXPECT_EQ(ledger.inflightCount(), 1U);
    EXPECT_EQ(ledger.lastCompletedSubmission(), 1U);

    ASSERT_TRUE(ledger.complete(*t2).has_value());
    EXPECT_TRUE(ledger.allClear());
    EXPECT_EQ(ledger.completedCount(), 2U);
}

TEST(NullSubmissionCompletionLedgerTest, CapacityFailureDoesNotLeakInflight)
{
    Render::NullSubmissionCompletionLedger ledger{1};
    auto t1 = ledger.beginSubmitted(10);
    ASSERT_TRUE(t1.has_value());
    auto t2 = ledger.beginSubmitted(11);
    ASSERT_FALSE(t2.has_value());
    EXPECT_EQ(t2.error().code, Render::RenderErrorCode::SubmissionCompletionLedgerFull);
    EXPECT_EQ(ledger.inflightCount(), 1U);
    ASSERT_TRUE(ledger.complete(*t1).has_value());
    EXPECT_TRUE(ledger.allClear());
}

TEST(RenderFramePacketTest, PinsReleaseOnComplete)
{
    g_releaseCount.store(0);
    Render::NullSubmissionCompletionLedger ledger{};
    Render::RenderFramePacket packet;

    ASSERT_TRUE(packet.beginFrame(3).has_value());
    ASSERT_TRUE(packet
                    .add(Render::FramePinKind::GlyphAtlas,
                         Render::FramePin{Render::FramePinKind::GlyphAtlas, 0, new int{1}, &countingRelease})
                    .has_value());
    ASSERT_TRUE(packet
                    .add(Render::FramePinKind::Surface,
                         Render::FramePin{Render::FramePinKind::Surface, 9, new int{2}, &countingRelease})
                    .has_value());
    EXPECT_EQ(packet.pinCount(), 2U);

    auto ticket = ledger.beginSubmitted(5);
    ASSERT_TRUE(ticket.has_value());
    ASSERT_TRUE(packet.attachSubmission(*ticket).has_value());
    EXPECT_EQ(packet.state(), Render::RenderFramePacket::State::Submitted);

    ASSERT_TRUE(packet.complete(ledger).has_value());
    EXPECT_EQ(packet.state(), Render::RenderFramePacket::State::Completed);
    EXPECT_EQ(packet.pinCount(), 0U);
    EXPECT_EQ(g_releaseCount.load(), 2);
    EXPECT_TRUE(ledger.allClear());
}

TEST(RenderFramePacketTest, AbandonReleasesPinsAndTicket)
{
    g_releaseCount.store(0);
    Render::NullSubmissionCompletionLedger ledger{};
    Render::RenderFramePacket packet;
    ASSERT_TRUE(packet.beginFrame(1).has_value());
    ASSERT_TRUE(packet
                    .add(Render::FramePinKind::Custom,
                         Render::FramePin{Render::FramePinKind::Custom, 0, new int{3}, &countingRelease})
                    .has_value());
    auto ticket = ledger.beginSubmitted(8);
    ASSERT_TRUE(ticket.has_value());
    ASSERT_TRUE(packet.attachSubmission(*ticket).has_value());
    ASSERT_TRUE(packet.abandon(&ledger).has_value());
    EXPECT_EQ(packet.state(), Render::RenderFramePacket::State::Abandoned);
    EXPECT_EQ(g_releaseCount.load(), 1);
    EXPECT_TRUE(ledger.allClear());
    EXPECT_EQ(ledger.abandonedCount(), 1U);
}

TEST(RenderFramePacketTest, CompleteSkippedReleasesWithoutTicket)
{
    g_releaseCount.store(0);
    Render::RenderFramePacket packet;
    ASSERT_TRUE(packet.beginFrame(2).has_value());
    ASSERT_TRUE(packet
                    .add(Render::FramePinKind::Custom,
                         Render::FramePin{Render::FramePinKind::Custom, 0, new int{4}, &countingRelease})
                    .has_value());
    ASSERT_TRUE(packet.completeSkipped().has_value());
    EXPECT_EQ(g_releaseCount.load(), 1);
    EXPECT_EQ(packet.state(), Render::RenderFramePacket::State::Completed);
}

TEST(RenderFramePacketTest, PinCapacityIsFixed)
{
    Render::RenderFramePacket packet;
    ASSERT_TRUE(packet.beginFrame(1).has_value());
    for (Core::u32 i = 0; i < Render::RenderFramePacket::MaxPins; ++i)
    {
        ASSERT_TRUE(packet
                        .add(Render::FramePinKind::Custom,
                             Render::FramePin{Render::FramePinKind::Custom, i, new int{static_cast<int>(i)},
                                              &countingRelease})
                        .has_value())
            << i;
    }
    auto overflow = packet.add(
        Render::FramePinKind::Custom,
        Render::FramePin{Render::FramePinKind::Custom, 99, new int{99}, &countingRelease});
    ASSERT_FALSE(overflow.has_value());
    EXPECT_EQ(overflow.error().code, Render::RenderErrorCode::FramePinCapacityExceeded);
    ASSERT_TRUE(packet.completeSkipped().has_value());
}

} // namespace
} // namespace Tina::Tests
