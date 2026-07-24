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

TEST(NullSubmissionCompletionLedgerTest, CompletionModeIsPresentSync)
{
    Render::NullSubmissionCompletionLedger ledger{};
    EXPECT_EQ(ledger.completionMode(), Render::SubmissionCompletionMode::PresentSync);
}

// Counting mock proves Host/packet call sites use the SPI, not a concrete Null type only.
class CountingMockSubmissionCompletionLedger final : public Render::ISubmissionCompletionLedger {
public:
    [[nodiscard]] Core::Result<Render::SubmissionTicket> beginSubmitted(Core::u64 submissionIndex) override
    {
        ++beginCalls;
        if (full)
        {
            return Core::failure(Render::RenderErrorCode::SubmissionCompletionLedgerFull, "mock full");
        }
        ++inflight;
        return Render::SubmissionTicket{.submissionIndex = submissionIndex, .open = true};
    }

    [[nodiscard]] Core::Status complete(Render::SubmissionTicket& ticket) noexcept override
    {
        ++completeCalls;
        if (!ticket.open)
        {
            return Core::success();
        }
        if (inflight == 0)
        {
            ticket.open = false;
            return Core::failure(Render::RenderErrorCode::InvalidSubmissionTicket, "mock empty");
        }
        --inflight;
        lastCompleted = ticket.submissionIndex;
        ticket.open = false;
        return Core::success();
    }

    [[nodiscard]] Core::Status abandon(Render::SubmissionTicket& ticket) noexcept override
    {
        ++abandonCalls;
        if (!ticket.open)
        {
            return Core::success();
        }
        if (inflight == 0)
        {
            ticket.open = false;
            return Core::failure(Render::RenderErrorCode::InvalidSubmissionTicket, "mock empty");
        }
        --inflight;
        ticket.open = false;
        return Core::success();
    }

    [[nodiscard]] Core::u32 inflightCount() const noexcept override { return inflight; }
    [[nodiscard]] bool allClear() const noexcept override { return inflight == 0; }
    [[nodiscard]] Render::SubmissionCompletionMode completionMode() const noexcept override
    {
        return Render::SubmissionCompletionMode::PresentSync;
    }

    Core::u32 beginCalls = 0;
    Core::u32 completeCalls = 0;
    Core::u32 abandonCalls = 0;
    Core::u32 inflight = 0;
    Core::u64 lastCompleted = 0;
    bool full = false;
};

TEST(ISubmissionCompletionLedgerPolymorphismTest, PacketCompleteUsesInjectedMock)
{
    g_releaseCount.store(0);
    CountingMockSubmissionCompletionLedger mock;
    Render::ISubmissionCompletionLedger& ledger = mock;
    Render::RenderFramePacket packet;

    ASSERT_TRUE(packet.beginFrame(1).has_value());
    ASSERT_TRUE(packet
                    .add(Render::FramePinKind::Custom,
                         Render::FramePin{Render::FramePinKind::Custom, 0, new int{11}, &countingRelease})
                    .has_value());
    auto ticket = ledger.beginSubmitted(42);
    ASSERT_TRUE(ticket.has_value());
    ASSERT_TRUE(packet.attachSubmission(*ticket).has_value());
    ASSERT_TRUE(packet.complete(ledger).has_value());

    EXPECT_EQ(mock.beginCalls, 1U);
    EXPECT_EQ(mock.completeCalls, 1U);
    EXPECT_EQ(mock.abandonCalls, 0U);
    EXPECT_EQ(mock.lastCompleted, 42U);
    EXPECT_TRUE(mock.allClear());
    EXPECT_EQ(g_releaseCount.load(), 1);
}

TEST(ISubmissionCompletionLedgerPolymorphismTest, PacketAbandonUsesInjectedMock)
{
    g_releaseCount.store(0);
    CountingMockSubmissionCompletionLedger mock;
    Render::ISubmissionCompletionLedger& ledger = mock;
    Render::RenderFramePacket packet;

    ASSERT_TRUE(packet.beginFrame(2).has_value());
    ASSERT_TRUE(packet
                    .add(Render::FramePinKind::Custom,
                         Render::FramePin{Render::FramePinKind::Custom, 0, new int{12}, &countingRelease})
                    .has_value());
    auto ticket = ledger.beginSubmitted(7);
    ASSERT_TRUE(ticket.has_value());
    ASSERT_TRUE(packet.attachSubmission(*ticket).has_value());
    ASSERT_TRUE(packet.abandon(&ledger).has_value());

    EXPECT_EQ(mock.beginCalls, 1U);
    EXPECT_EQ(mock.completeCalls, 0U);
    EXPECT_EQ(mock.abandonCalls, 1U);
    EXPECT_TRUE(mock.allClear());
    EXPECT_EQ(g_releaseCount.load(), 1);
}

TEST(BgfxSubmissionCompletionLedgerTest, PresentSyncBalancesLikeNullAndExposesFenceHooks)
{
    Render::BgfxSubmissionCompletionLedger ledger{4};
    EXPECT_EQ(ledger.completionMode(), Render::SubmissionCompletionMode::PresentSync);

    auto t1 = ledger.beginSubmitted(100);
    ASSERT_TRUE(t1.has_value());
    EXPECT_EQ(ledger.inflightCount(), 1U);

    // Fence hooks are no-ops while still present-sync.
    ledger.notePresentReturned(100);
    EXPECT_EQ(ledger.pollGpuFences(), 0U);
    EXPECT_EQ(ledger.inflightCount(), 1U);

    ASSERT_TRUE(ledger.complete(*t1).has_value());
    EXPECT_TRUE(ledger.allClear());
    EXPECT_EQ(ledger.lastCompletedSubmission(), 100U);
    EXPECT_EQ(ledger.completedCount(), 1U);
}

TEST(BgfxSubmissionCompletionLedgerTest, PacketCompleteThroughInterface)
{
    g_releaseCount.store(0);
    Render::BgfxSubmissionCompletionLedger concrete{};
    Render::ISubmissionCompletionLedger& ledger = concrete;
    Render::RenderFramePacket packet;

    ASSERT_TRUE(packet.beginFrame(9).has_value());
    ASSERT_TRUE(packet
                    .add(Render::FramePinKind::Surface,
                         Render::FramePin{Render::FramePinKind::Surface, 1, new int{3}, &countingRelease})
                    .has_value());
    auto ticket = ledger.beginSubmitted(3);
    ASSERT_TRUE(ticket.has_value());
    ASSERT_TRUE(packet.attachSubmission(*ticket).has_value());
    ASSERT_TRUE(packet.complete(ledger).has_value());
    EXPECT_EQ(g_releaseCount.load(), 1);
    EXPECT_TRUE(ledger.allClear());
}

TEST(RenderFramePacketTest, PinsReleaseOnComplete)
{
    g_releaseCount.store(0);
    Render::NullSubmissionCompletionLedger ledger{};
    Render::ISubmissionCompletionLedger& ledgerSpi = ledger;
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

    auto ticket = ledgerSpi.beginSubmitted(5);
    ASSERT_TRUE(ticket.has_value());
    ASSERT_TRUE(packet.attachSubmission(*ticket).has_value());
    EXPECT_EQ(packet.state(), Render::RenderFramePacket::State::Submitted);

    ASSERT_TRUE(packet.complete(ledgerSpi).has_value());
    EXPECT_EQ(packet.state(), Render::RenderFramePacket::State::Completed);
    EXPECT_EQ(packet.pinCount(), 0U);
    EXPECT_EQ(g_releaseCount.load(), 2);
    EXPECT_TRUE(ledgerSpi.allClear());
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
